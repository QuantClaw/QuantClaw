// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import type {
  ProviderEntry,
  ProviderPlugin,
  SidecarStartupConfig,
} from "./types.js";

export const BUILTIN_LOCAL_PROVIDER_PLUGIN_ID = "builtin";
export const BUILTIN_LOCAL_PROVIDER_ID = "local";
export const DEFAULT_LOCAL_PROVIDER_URL = "http://127.0.0.1:8081";

export interface ProviderRegistryTarget {
  providers: ProviderEntry[];
}

function resolveLocalProviderUrl(config: SidecarStartupConfig): string {
  const configuredUrl = config.local_provider_url?.trim();
  return configuredUrl && configuredUrl.length > 0
    ? configuredUrl
    : DEFAULT_LOCAL_PROVIDER_URL;
}

export function createBuiltinLocalProvider(
  config: SidecarStartupConfig,
): ProviderPlugin {
  return {
    id: BUILTIN_LOCAL_PROVIDER_ID,
    label: "Local Provider",
    aliases: ["local"],
    baseUrl: resolveLocalProviderUrl(config),
    auth: [
      {
        id: "none",
        label: "No authentication",
        hint: "Built-in local provider does not require credentials.",
        kind: "none",
        run: async () => ({}),
      },
    ],
  };
}

export function createBuiltinLocalProviderEntry(
  config: SidecarStartupConfig,
): ProviderEntry {
  return {
    pluginId: BUILTIN_LOCAL_PROVIDER_PLUGIN_ID,
    provider: createBuiltinLocalProvider(config),
  };
}

export function seedBuiltinProviders(
  registries: ProviderRegistryTarget,
  config: SidecarStartupConfig,
): void {
  if (registries.providers.some((entry) => entry.provider.id === BUILTIN_LOCAL_PROVIDER_ID)) {
    return;
  }

  registries.providers.push(createBuiltinLocalProviderEntry(config));
}
