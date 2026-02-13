import { writable, get } from 'svelte/store';

/**
 * Error severity levels for display styling.
 */
export type ErrorSeverity = 'error' | 'warning' | 'info' | 'success';

/**
 * Error message category for user-friendly message mapping.
 */
export type ErrorCategory = 
  | 'network'
  | 'api'
  | 'auth'
  | 'validation'
  | 'game_state'
  | 'general'
  | 'unknown';

/**
 * Complete error data structure.
 */
export interface AppError {
  id: string;
  user_message: string;
  severity: ErrorSeverity;
  category: ErrorCategory;
  context?: string;
  original_error: unknown;
  technical_details: string;
  timestamp: number;
  dismissible: boolean;
  auto_dismiss_ms: number;
  auto_dismissed: boolean;
}

/**
 * Options for creating/dispatching errors.
 */
export interface ErrorOptions {
  user_message?: string;
  severity?: ErrorSeverity;
  category?: ErrorCategory;
  context?: string;
  dismissible?: boolean;
  auto_dismiss_ms?: number;
}

/**
 * Store containing array of active errors.
 */
export const errors = writable<Array<AppError>>([]);

/**
 * Add an error to the store.
 */
export function addError(error: AppError): void {
  errors.update(current => [...current, error]);
  
  if (error.auto_dismiss_ms > 0) {
    setTimeout(() => {
      errors.update(current => 
        current.map(e => 
          e.id === error.id 
            ? { ...e, auto_dismissed: true }
            : e
        )
      );
      
      setTimeout(() => {
        removeError(error.id);
      }, 500);
    }, error.auto_dismiss_ms);
  }
}

/**
 * Remove an error from the store.
 */
export function removeError(errorId: string): void {
  errors.update(current => current.filter(e => e.id !== errorId));
}

/**
 * Clear all errors from the store.
 */
export function clearAllErrors(): void {
  errors.set([]);
}

/**
 * Get active (not auto-dismissed) errors.
 */
export function getActiveErrors(): Array<AppError> {
  return get(errors).filter(e => !e.auto_dismissed);
}

function extractTechnicalDetails(error: unknown): string {
  if (error instanceof Error) {
    return `${error.message}\n${error.stack || ''}`;
  }
  if (typeof error === 'string') {
    return error;
  }
  if (error && typeof error === 'object') {
    try {
      return JSON.stringify(error, null, 2);
    } catch {
      return String(error);
    }
  }
  return String(error);
}

function categorizeError(error: unknown, technical_details: string): ErrorCategory {
  const combined = technical_details.toLowerCase();
  
  if (combined.includes('network') || combined.includes('fetch') || combined.includes('connection')) {
    return 'network';
  }
  if (combined.includes('auth') || combined.includes('login') || combined.includes('token')) {
    return 'auth';
  }
  if (combined.includes('api') || combined.includes('endpoint')) {
    return 'api';
  }
  if (combined.includes('fiefdom') || combined.includes('character') || combined.includes('game')) {
    return 'game_state';
  }
  
  return 'unknown';
}

function getDefaultAutoDismissForSeverity(severity: ErrorSeverity): number {
  switch (severity) {
    case 'error': return 5000;
    case 'warning': return 8000;
    case 'info': return 6000;
    case 'success': return 4000;
    default: return 5000;
  }
}

/**
 * Get user-friendly message for a given error type/message.
 */
export function getUserFriendlyMessage(category: ErrorCategory, original_message: string): string {
  const lower_msg = original_message.toLowerCase();
  
  switch (category) {
    case 'network':
      if (lower_msg.includes('failed to connect') || lower_msg.includes('econnrefused')) {
        return 'Unable to connect to the server. Please check your internet connection.';
      }
      if (lower_msg.includes('timeout')) {
        return 'The request timed out. Please try again.';
      }
      return 'A network error occurred. Please check your connection and try again.';
    
    case 'auth':
      if (lower_msg.includes('invalid') && lower_msg.includes('password')) {
        return 'Invalid username or password.';
      }
      if (lower_msg.includes('not found')) {
        return 'User not found. Please check your username.';
      }
      if (lower_msg.includes('token')) {
        return 'Your session has expired. Please log in again.';
      }
      return 'An authentication error occurred.';
    
    case 'api':
      if (lower_msg.includes('fiefdom not found')) {
        return 'Fiefdom data not found. The game area may not have been created yet.';
      }
      if (lower_msg.includes('character not found')) {
        return 'Character data not found. Please try again.';
      }
      if (lower_msg.includes('server_error')) {
        return 'The server encountered an error. Please try again later.';
      }
      return 'An error occurred communicating with the game server.';
    
    case 'validation':
      if (lower_msg.includes('password') && lower_msg.includes('match')) {
        return 'Passwords do not match.';
      }
      if (lower_msg.includes('username')) {
        return 'Please enter a valid username.';
      }
      return 'Please check your input and try again.';
    
    case 'game_state':
      if (lower_msg.includes('fiefdom')) {
        return 'Unable to load fiefdom data.';
      }
      if (lower_msg.includes('game config')) {
        return 'Unable to load game configuration.';
      }
      return 'An error occurred loading game data.';
    
    case 'general':
    default:
      return 'An unexpected error occurred. Please try again.';
  }
}

/**
 * Global error handler function.
 * 
 * Creates an AppError object, logs full details to console, and adds to error store.
 * 
 * @param user_message - User-friendly message to display
 * @param error - The original error (Error object, string, etc.)
 * @param options - Additional options for error creation
 */
export function handleError(
  user_message: string,
  error: unknown,
  options: ErrorOptions = {}
): string {
  const error_id = `err_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  
  const technical_details = extractTechnicalDetails(error);
  const category = options.category || categorizeError(error, technical_details);
  
  const app_error: AppError = {
    id: error_id,
    user_message: user_message,
    severity: options.severity || 'error',
    category,
    context: options.context,
    original_error: error,
    technical_details,
    timestamp: Date.now(),
    dismissible: options.dismissible ?? true,
    auto_dismiss_ms: options.auto_dismiss_ms ?? getDefaultAutoDismissForSeverity(options.severity || 'error'),
    auto_dismissed: false
  };
  
  console.error(
    `[${app_error.category.toUpperCase()}] ${user_message}`,
    {
      error_id: error_id,
      category: app_error.category,
      severity: app_error.severity,
      context: app_error.context,
      original_error: error,
      technical_details: app_error.technical_details
    }
  );
  
  addError(app_error);
  
  return error_id;
}

/**
 * Create a warning (non-blocking error).
 */
export function handleWarning(
  user_message: string,
  options: ErrorOptions = {}
): string {
  return handleError(user_message, new Error('Warning'), {
    severity: 'warning',
    ...options
  });
}

/**
 * Show an informational message.
 */
export function handleInfo(
  user_message: string,
  options: ErrorOptions = {}
): string {
  return handleError(user_message, new Error('Info'), {
    severity: 'info',
    ...options
  });
}

/**
 * Show a success message.
 */
export function handleSuccess(
  user_message: string,
  options: ErrorOptions = {}
): string {
  return handleError(user_message, new Error('Success'), {
    severity: 'success',
    ...options
  });
}
