export interface DigitalCredentialResponse {
  protocol: string;
  data: Record<string, unknown> | string;
}

export interface VerifierMetadata {
  response_type: string;
  nonce: string;
  client_metadata: {
    vp_formats_supported: Record<string, boolean>;
    jwks?: Record<string, unknown>;
  };
  dcql_query: {
    credentials: Array<{
      id: string;
      format: string;
      meta: Record<string, unknown>;
      claims: Array<{
        path: string[];
        values?: unknown[];
      }>;
    }>;
  };
}

function generateNonce(): string {
  const array = new Uint8Array(32);
  crypto.getRandomValues(array);
  return Array.from(array, byte => byte.toString(16).padStart(2, '0')).join('');
}

/**
 * Checks if the browser supports the Digital Credentials API.
 * The API is available in some Chromium-based browsers.
 * 
 * @param none
 * @returns Promise<boolean> - true if DigitalCredential is defined
 * 
 * Usage: Called before attempting age verification
 */
export function checkDigitalCredentialsSupport(): Promise<boolean> {
  return Promise.resolve(typeof DigitalCredential !== 'undefined');
}

/**
 * Gets list of digital credential protocols supported by the browser.
 * Queries browser for supported protocols using DigitalCredential API.
 * 
 * @param none
 * @returns string[] - Array of supported protocol names
 * 
 * Usage: Called to determine which protocol to use for verification
 */
export function getUserSupportedProtocols(): string[] {
  if (typeof DigitalCredential === 'undefined') {
    return [];
  }

  const protocols = [
    'openid4vp-v1-signed',
    'openid4vp-v1-unsigned',
    'org-iso-mdoc'
  ];

  return protocols.filter(protocol =>
    DigitalCredential.userAgentAllowsProtocol(protocol)
  );
}

/**
 * Generates verifier metadata for age verification request.
 * Creates a query asking for proof that holder is 18 or older.
 * 
 * @param sessionId - Unique identifier for this verification session
 * @returns VerifierMetadata - Metadata for credential request
 * 
 * Usage: Called before requesting age verification from user
 */
export function getVerifierMetadata(sessionId: string): VerifierMetadata {
  return {
    response_type: 'vp_token',
    nonce: generateNonce(),
    client_metadata: {
      vp_formats_supported: {
        'openid4vp_jwt': true,
        'openid4vp_vc_sd_jwt': true
      }
    },
    dcql_query: {
      credentials: [
        {
          id: 'age_check',
          format: 'dc+sd-jwt',
          meta: { vct_values: ['*'] },
          claims: [
            { path: ['age_equal_or_over', '18'] }
          ]
        }
      ]
    }
  };
}

/**
 * Requests age verification from the user using Digital Credentials API.
   * Prompts user to present a credential proving they are 18+.
 * Returns null if API unavailable, cancelled, or fails.
 * 
 * @param sessionId - Optional session identifier (defaults to 'create-account')
 * @returns Promise<DigitalCredentialResponse | null> - Credential response or null
 * 
 * Usage: Called when user checks adult flag on account creation
 */
export async function requestAgeVerification(
  sessionId?: string
): Promise<DigitalCredentialResponse | null> {
  if (typeof DigitalCredential === 'undefined') {
    console.warn('Digital Credentials API not supported');
    return null;
  }

  const supportedProtocols = getUserSupportedProtocols();
  if (supportedProtocols.length === 0) {
    console.warn('No supported protocols available');
    return null;
  }

  const protocol = supportedProtocols[0];
  const verifierMetadata = getVerifierMetadata(sessionId || 'create-account');

  try {
    const credential = await navigator.credentials.get({
      digital: {
        requests: [
          {
            protocol: protocol,
            data: verifierMetadata
          }
        ]
      },
      mediation: 'required'
    }) as DigitalCredentialResponse | null;

    if (!credential) {
      return null;
    }

    return {
      protocol: credential.protocol,
      data: credential.data
    };
  } catch (error) {
    console.error('Age verification failed:', error);
    return null;
  }
}

/**
 * Type guard to check if an object matches DigitalCredentialResponse structure.
 * Validates that object has required protocol and data fields.
 * 
 * @param obj - Object to check
 * @returns boolean - true if object is a valid DigitalCredentialResponse
 * 
 * Usage: Used to validate credential responses from API
 */
export function isDigitalCredential(obj: unknown): obj is DigitalCredentialResponse {
  if (typeof obj !== 'object' || obj === null) {
    return false;
  }
  const objRecord = obj as Record<string, unknown>;
  return (
    typeof objRecord.protocol === 'string' &&
    (typeof objRecord.data === 'object' || typeof objRecord.data === 'string')
  );
}
