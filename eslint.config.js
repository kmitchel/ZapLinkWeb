const js = require('@eslint/js');
const nodePlugin = require('eslint-plugin-n');
const prettierConfig = require('eslint-config-prettier');

module.exports = [
    js.configs.recommended,
    nodePlugin.configs['flat/recommended'],
    {
        files: ['**/*.js'],
        languageOptions: {
            ecmaVersion: 2021,
            sourceType: 'commonjs',
            globals: {
                process: 'readonly',
                __dirname: 'readonly',
                __filename: 'readonly',
                require: 'readonly',
                module: 'readonly',
                exports: 'readonly',
                console: 'readonly',
                setInterval: 'readonly',
                clearInterval: 'readonly',
                setTimeout: 'readonly',
                clearTimeout: 'readonly',
                Buffer: 'readonly',
                URLSearchParams: 'readonly',
                history: 'readonly'
            }
        },
        rules: {
            ...prettierConfig.rules,
            'no-console': 'off',
            'n/no-unsupported-features/es-syntax': 'off',
            'n/no-missing-require': [
                'error',
                {
                    allowModules: []
                }
            ],
            'no-unused-vars': [
                'warn',
                {
                    argsIgnorePattern: '^_',
                    varsIgnorePattern: '^_'
                }
            ],
            'n/no-unpublished-require': 'off',
            'n/no-extraneous-require': 'off'
        }
    },
    {
        ignores: ['node_modules/**', 'recordings/**', '*.ts', '*.m3u', '*.xml', 'logos.json', 'package-lock.json']
    }
];
