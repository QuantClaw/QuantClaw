// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import { describe, expect, it } from "vitest";
import {
  BUILTIN_LOCAL_PROVIDER_ID,
  BUILTIN_LOCAL_PROVIDER_PLUGIN_ID,
  DEFAULT_LOCAL_PROVIDER_URL,
  createBuiltinLocalProvider,
  seedBuiltinProviders,
} from "../src/local-provider.js";
import type { ProviderEntry, SidecarStartupConfig } from "../src/types.js";

describe("local-provider helper", () => {
  it("creates a builtin local provider with a no-auth method", () => {
    const provider = createBuiltinLocalProvider({
      enabled_plugins: [],
    });

    expect(provider.id).toBe(BUILTIN_LOCAL_PROVIDER_ID);
    expect(provider.baseUrl).toBe(DEFAULT_LOCAL_PROVIDER_URL);
    expect(provider.auth).toHaveLength(1);
    expect(provider.auth[0].kind).toBe("none");
    expect(provider.auth[0].id).toBe("none");
  });

  it("uses the configured local provider url when seeding", () => {
    const startupConfig: SidecarStartupConfig = {
      enabled_plugins: [],
      local_provider_url: "http://127.0.0.1:9999",
    };
    // The helper only needs a providers array, so keep the fixture lightweight.
    const target: { providers: ProviderEntry[] } = { providers: [] };
    seedBuiltinProviders(target, startupConfig);

    expect(target.providers).toHaveLength(1);
    expect(target.providers[0].pluginId).toBe(BUILTIN_LOCAL_PROVIDER_PLUGIN_ID);
    expect(target.providers[0].provider.baseUrl).toBe("http://127.0.0.1:9999");
  });

  it("does not duplicate an existing local provider", () => {
    const startupConfig: SidecarStartupConfig = {
      enabled_plugins: [],
      local_provider_url: "http://127.0.0.1:9999",
    };
    const target = {
      providers: [
        {
          pluginId: "plugin-x",
          provider: createBuiltinLocalProvider(startupConfig),
        },
      ],
    };

    seedBuiltinProviders(target, startupConfig);

    expect(target.providers).toHaveLength(1);
  });
});
