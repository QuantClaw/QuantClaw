import { defineConfig } from 'vitepress'

const enNav = [
  { text: 'Home', link: '/' },
  { text: 'Getting Started', link: '/guide/getting-started' },
  { text: 'Features', link: '/guide/features' },
  { text: 'Architecture', link: '/guide/architecture' },
  { text: 'Plugins', link: '/guide/plugins' },
  { text: 'CLI Reference', link: '/guide/cli-reference' },
  { text: 'GitHub', link: 'https://github.com/QuantClaw/QuantClaw' },
]

const zhNav = [
  { text: '主页', link: '/zh/' },
  { text: '快速开始', link: '/zh/guide/getting-started' },
  { text: '特性', link: '/zh/guide/features' },
  { text: '架构', link: '/zh/guide/architecture' },
  { text: '插件', link: '/zh/guide/plugins' },
  { text: 'CLI 参考', link: '/zh/guide/cli-reference' },
  { text: 'GitHub', link: 'https://github.com/QuantClaw/QuantClaw' },
]

const enSidebar = {
  '/guide/': [
    {
      text: 'Getting Started',
      items: [
        { text: 'Quick Start', link: '/guide/getting-started' },
        { text: 'Installation', link: '/guide/installation' },
        { text: 'Configuration', link: '/guide/configuration' },
      ],
    },
    {
      text: 'Features',
      items: [
        { text: 'Core Features', link: '/guide/features' },
        { text: 'Architecture', link: '/guide/architecture' },
        { text: 'CLI Reference', link: '/guide/cli-reference' },
      ],
    },
    {
      text: 'Development',
      items: [
        { text: 'Plugin Development', link: '/guide/plugins' },
        { text: 'Building from Source', link: '/guide/building' },
        { text: 'Contributing', link: '/guide/contributing' },
      ],
    },
  ],
}

const zhSidebar = {
  '/zh/guide/': [
    {
      text: '快速开始',
      items: [
        { text: '快速上手', link: '/zh/guide/getting-started' },
        { text: '安装说明', link: '/zh/guide/installation' },
        { text: '配置参考', link: '/zh/guide/configuration' },
      ],
    },
    {
      text: '功能',
      items: [
        { text: '核心特性', link: '/zh/guide/features' },
        { text: '架构说明', link: '/zh/guide/architecture' },
        { text: 'CLI 参考', link: '/zh/guide/cli-reference' },
      ],
    },
    {
      text: '开发',
      items: [
        { text: '插件开发', link: '/zh/guide/plugins' },
        { text: '从源码构建', link: '/zh/guide/building' },
        { text: '参与贡献', link: '/zh/guide/contributing' },
      ],
    },
  ],
}

export default defineConfig({
  title: 'QuantClaw',
  description: 'High-performance C++17 implementation of OpenClaw - AI agent framework with persistent memory, browser control, and plugin ecosystem',

  // GitHub Pages: deployed at quantclaw.github.io/QuantClaw/
  base: '/QuantClaw/',
  cleanUrls: true,

  head: [
    ['meta', { name: 'theme-color', content: '#2d3748' }],
    ['meta', { name: 'og:type', content: 'website' }],
    ['meta', { name: 'og:image', content: '/QuantClaw/logo.png' }],
    ['link', { rel: 'icon', href: '/QuantClaw/favicon.ico' }],
  ],

  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      themeConfig: {
        nav: enNav,
        sidebar: enSidebar,
        editLink: {
          pattern: 'https://github.com/QuantClaw/QuantClaw/edit/main/website/:path',
          text: 'Edit this page on GitHub',
        },
        footer: {
          message: 'Released under the Apache 2.0 License.',
          copyright: 'Copyright © 2024-2026 QuantClaw Contributors',
        },
        docFooter: {
          prev: 'Previous',
          next: 'Next',
        },
      },
    },
    zh: {
      label: '中文',
      lang: 'zh-CN',
      link: '/zh/',
      themeConfig: {
        nav: zhNav,
        sidebar: zhSidebar,
        editLink: {
          pattern: 'https://github.com/QuantClaw/QuantClaw/edit/main/website/:path',
          text: '在 GitHub 上编辑此页',
        },
        footer: {
          message: '基于 Apache 2.0 协议发布。',
          copyright: 'Copyright © 2024-2026 QuantClaw 贡献者',
        },
        docFooter: {
          prev: '上一页',
          next: '下一页',
        },
        outlineTitle: '本页目录',
        lastUpdatedText: '最后更新',
        returnToTopLabel: '返回顶部',
        sidebarMenuLabel: '目录',
        darkModeSwitchLabel: '主题',
      },
    },
  },

  themeConfig: {
    logo: '/logo.png',
    search: {
      provider: 'local',
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/QuantClaw/QuantClaw' },
    ],
  },

  markdown: {
    lineNumbers: false,
  },
})
