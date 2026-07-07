// Global DigitalCredential interface from the W3C Digital Credentials API spec
// Not yet in TypeScript lib types; declare here for type safety.
interface DigitalCredentialConstructor {
  new(): DigitalCredentialInstance;
  prototype: DigitalCredentialInstance;
  userAgentAllowsProtocol(protocol: string): boolean;
}
interface DigitalCredentialInstance extends Credential {
  protocol: string;
  data: Record<string, unknown> | string;
}
declare var DigitalCredential: DigitalCredentialConstructor | undefined;

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

/**
 * Detects if the browser is Brave.
 * Uses navigator.brave.isBrave() which Brave 1.0+ exposes.
 * Falls back to false if the API is unavailable.
 * 
 * @param none
 * @returns Promise<boolean> - true if browser identifies as Brave
 * 
 * Usage: Called to customize Digital Credentials error messaging
 */
export async function isBraveBrowser(): Promise<boolean> {
  try {
    const brave = (navigator as { brave?: { isBrave: () => Promise<boolean> } }).brave;
    if (brave) {
      return await brave.isBrave();
    }
  } catch {
    // fall through
  }
  return false;
}

function generateNonce(): string {
  const array = new Uint8Array(32);
  crypto.getRandomValues(array);
  return Array.from(array, byte => byte.toString(16).padStart(2, '0')).join('');
}

/**
 * Checks if the browser supports the Digital Credentials API.
 * Feature-detects by attempting a silent digital credential request.
 * Some browsers (e.g. Brave) support navigator.credentials.get({digital:...})
 * but do not expose the DigitalCredential global constructor.
 * 
 * @param none
 * @returns Promise<boolean> - true if the browser accepts digital credential requests
 * 
 * Usage: Called before attempting age verification
 */
export async function checkDigitalCredentialsSupport(): Promise<boolean> {
  // First try: spec-compliant typeof check (Chromium flag exposes DigitalCredential)
  if (typeof DigitalCredential !== 'undefined') {
    return true;
  }

  // Second try: feature-detect via actual call
  try {
    await navigator.credentials.get({
      digital: { requests: [] },
      mediation: 'silent'
    } as CredentialRequestOptions);
    return true;
  } catch (error) {
    const navRecord = navigator as unknown as Record<string, unknown>;
    console.debug('[DC Debug] navigator.credentials.get({digital:...}) failed:', {
      name: error instanceof Error ? error.name : typeof error,
      message: error instanceof Error ? error.message : String(error),
      stack: error instanceof Error ? error.stack : undefined,
      typeof_DigitalCredential: typeof DigitalCredential,
      typeof_brave: typeof navRecord.brave,
      user_agent: navigator.userAgent
    });
    return false;
  }
}

/**
 * Checks if the DigitalCredential global constructor exists (spec-compliant check).
 * 
 * @param none
 * @returns boolean - true if typeof DigitalCredential !== 'undefined'
 * 
 * Usage: Quick check whether the browser exposes the spec interface
 */
export function checkDigitalCredentialGlobal(): boolean {
  return typeof DigitalCredential !== 'undefined';
}

/**
 * Test utility: attempts a real digital credential request with a non-empty protocol
 * to distinguish "API not supported" from "empty requests rejected by spec validation".
 * 
 * @param none
 * @returns Promise<object> - Diagnostic info about the request attempt
 * 
 * Usage: Call from console via __debugDC.testGetWithRealRequest()
 */
export async function testGetWithRealRequest(): Promise<Record<string, unknown>> {
  const navRecord = navigator as unknown as Record<string, unknown>;
  const diagnostics: Record<string, unknown> = {
    typeof_DigitalCredential: typeof DigitalCredential,
    typeof_brave: typeof navRecord.brave,
    user_agent: navigator.userAgent,
  };

  if (typeof DigitalCredential !== 'undefined') {
    const protocols = ['openid4vp-v1-signed', 'openid4vp-v1-unsigned', 'org-iso-mdoc'];
    const supported: string[] = [];
    for (const p of protocols) {
      try {
        if (DigitalCredential.userAgentAllowsProtocol(p)) {
          supported.push(p);
        }
      } catch {
        // skip
      }
    }
    diagnostics.supported_protocols = supported;
  }

  try {
    const result = await navigator.credentials.get({
      digital: {
        requests: [{
          protocol: 'openid4vp-v1-unsigned',
          data: {}
        }]
      },
      mediation: 'silent'
    } as CredentialRequestOptions);
    diagnostics.digital_get_result = result;
    diagnostics.digital_get_success = true;
  } catch (error) {
    diagnostics.digital_get_success = false;
    diagnostics.digital_get_error = error instanceof Error
      ? { name: error.name, message: error.message }
      : String(error);
  }

  return diagnostics;
}

// Expose debug utilities globally for console inspection
if (typeof window !== 'undefined') {
  (window as unknown as Record<string, unknown>).__debugDC = {
    checkSupport: checkDigitalCredentialsSupport,
    isBrave: isBraveBrowser,
    checkDigitalCredentialGlobal: checkDigitalCredentialGlobal,
    testGetWithRealRequest: testGetWithRealRequest,
  };
}

/**
 * Gets list of known digital credential protocols.
 * Returns all standard protocols — the browser accepts or rejects
 * at request time; pre-filtering via DigitalCredential.userAgentAllowsProtocol
 * is unreliable across Chromium forks (e.g. Brave doesn't expose it).
 * 
 * @param none
 * @returns string[] - Array of known protocol names
 * 
 * Usage: Called to determine which protocol to attempt for verification
 */
export function getUserSupportedProtocols(): string[] {
  return [
    'openid4vp-v1-signed',
    'openid4vp-v1-unsigned',
    'org-iso-mdoc'
  ];
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
    } as CredentialRequestOptions) as DigitalCredentialResponse | null;

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
