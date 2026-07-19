import js from '@eslint/js';
import tseslint from 'typescript-eslint';
import globals from 'globals';

// Lints the Electron main-process source only. Build scripts (scripts/,
// forge.config.ts) and native/ are intentionally out of scope.
export default tseslint.config(
  {
    ignores: ['dist', 'out', 'node_modules', 'native', 'scripts', '*.config.ts']
  },
  {
    files: ['src/**/*.ts'],
    extends: [js.configs.recommended, ...tseslint.configs.recommended],
    languageOptions: {
      globals: { ...globals.node }
    },
    rules: {
      '@typescript-eslint/no-unused-vars': [
        'error',
        { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }
      ]
    }
  }
);
