#include "AvatarUploadManager.hpp"

#include <exception>
#include <iostream>
#include <utility>

#include "Base64.hpp"
#include "Hash.hpp"
#include "ImageReader.hpp"
#include "avatars/AvatarKey.hpp"
#include "avatars/AvatarValidation.hpp"

namespace {

avatar_upload_result process_avatar_upload(avatar_upload_task task,
                                           avatar_store& store) {
    avatar_upload_result result;
    result.character_id = task.character_id;

    const auto format = parse_avatar_format(task.format);
    if (!format) {
        result.error = "invalid_avatar_format";
        return result;
    }

    std::string bytes;
    if (!base64_decode(task.image_base64, bytes)) {
        result.error = "invalid_avatar";
        return result;
    }
    if (bytes.empty()) {
        result.error = "invalid_avatar";
        return result;
    }
    if (bytes.size() > k_avatar_max_upload_bytes) {
        result.error = "avatar_too_large";
        return result;
    }
    if (!bytes_match_format(*format, bytes)) {
        result.error = "invalid_avatar";
        return result;
    }

    const ImageDimensions dimensions = get_image_dimensions_from_memory(bytes);
    if (dimensions.width > 256 || dimensions.height > 256) {
        result.error = "invalid_avatar_dimensions";
        return result;
    }

    const std::string digest = sha256_hex(bytes);
    result.sha256 = digest;
    result.key = make_avatar_key(task.character_id, digest, avatar_extension(*format));

    std::string store_error;
    if (!store.put(result.key, bytes, avatar_content_type(*format), store_error)) {
        result.error = "avatar_storage_unavailable";
        result.detail = store_error;
        return result;
    }

    result.public_url = store.public_url(store.active_kind(), result.key);
    result.ok = true;
    return result;
}

} // namespace

avatar_upload_manager::avatar_upload_manager(avatar_store& store,
                                             thread_pool& upload_pool,
                                             thread_pool& maintenance_pool)
    : store_(store),
      upload_pool_(upload_pool),
      maintenance_pool_(maintenance_pool) {}

bool avatar_upload_manager::try_begin(int character_id) {
    if (character_id <= 0) return false;
    return in_flight_.insert(character_id).second;
}

void avatar_upload_manager::finish(int character_id) {
    in_flight_.erase(character_id);
}

bool avatar_upload_manager::submit(
    avatar_upload_task task,
    std::function<void(avatar_upload_result)> completion) {
    const int character_id = task.character_id;
    return upload_pool_.post(
        [this, character_id, task = std::move(task),
         completion = std::move(completion)]() mutable {
            avatar_upload_result result;
            try {
                result = process_avatar_upload(std::move(task), store_);
            } catch (const std::exception& e) {
                result.character_id = character_id;
                result.error = "avatar_upload_failed";
                result.detail = e.what();
            } catch (...) {
                result.character_id = character_id;
                result.error = "avatar_upload_failed";
                result.detail = "unknown exception";
            }
            if (!result.ok) {
                std::cerr << "avatar upload failed for character " << character_id
                          << ": " << result.error;
                if (!result.detail.empty()) std::cerr << " (" << result.detail << ")";
                std::cerr << std::endl;
            }
            if (completion) completion(std::move(result));
        });
}

void avatar_upload_manager::shutdown() {
    upload_pool_.shutdown();
    maintenance_pool_.shutdown();
}
