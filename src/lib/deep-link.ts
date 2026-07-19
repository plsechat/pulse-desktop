export const DEEP_LINK_SCHEME = 'pulse';

export interface DeepLinkTarget {
  /** Origin of the server to load, e.g. "https://pulse.example.com". */
  serverUrl: string;
  /** Invite code to consume on load, if any. */
  inviteCode: string | null;
}

/**
 * Parse a `pulse://` deep link into a server origin + optional invite code.
 *
 * Accepted shape (matches what the web client's "Open in Desktop app"
 * affordance emits):
 *   pulse://join?server=<encoded-origin>&invite=<code>
 *
 * The server origin is validated to be http/https; anything else is rejected
 * so a link can't point the app at a non-web URL.
 */
export function parseDeepLink(rawUrl: string): DeepLinkTarget | null {
  let url: URL;
  try {
    url = new URL(rawUrl);
  } catch {
    return null;
  }

  if (url.protocol !== `${DEEP_LINK_SCHEME}:`) return null;

  const server = url.searchParams.get('server');
  if (!server) return null;

  let origin: URL;
  try {
    origin = new URL(server);
  } catch {
    return null;
  }
  if (origin.protocol !== 'http:' && origin.protocol !== 'https:') return null;

  const invite = url.searchParams.get('invite');
  return {
    serverUrl: origin.origin,
    inviteCode: invite && invite.trim() !== '' ? invite : null
  };
}

/** Build the full page URL to load for a parsed deep-link target. */
export function targetUrl(target: DeepLinkTarget): string {
  return target.inviteCode
    ? `${target.serverUrl}/?invite=${encodeURIComponent(target.inviteCode)}`
    : target.serverUrl;
}
