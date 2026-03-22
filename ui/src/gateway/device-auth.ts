export type DeviceAuthPayloadParams = {
  deviceId: string;
  clientId: string;
  clientMode: string;
  role: string;
  scopes: string[];
  signedAtMs: number;
  token?: string | null;
  nonce: string;
};

function assertPayloadSegment(value: string, label: string): void {
  if (value.includes("|")) {
    throw new Error(`${label} must not contain "|"`);
  }
}

export function buildDeviceAuthPayload(params: DeviceAuthPayloadParams): string {
  assertPayloadSegment(params.deviceId, "deviceId");
  assertPayloadSegment(params.clientId, "clientId");
  assertPayloadSegment(params.clientMode, "clientMode");
  assertPayloadSegment(params.role, "role");
  assertPayloadSegment(params.nonce, "nonce");
  const scopes = params.scopes.join(",");
  const token = params.token ?? "";
  assertPayloadSegment(scopes, "scopes");
  assertPayloadSegment(token, "token");
  return [
    "v2",
    params.deviceId,
    params.clientId,
    params.clientMode,
    params.role,
    scopes,
    String(params.signedAtMs),
    token,
    params.nonce,
  ].join("|");
}
