#!/usr/bin/env node
const fs = require('node:fs');
const config = JSON.parse(fs.readFileSync(process.argv.at(-1), 'utf8'));
if (config.ignoreTerm) process.on('SIGTERM', () => {});
fs.writeFileSync(config.ready, String(process.pid));
if (config.delay !== undefined) {
  setTimeout(() => {
    process.stderr.write(config.echoContext ? JSON.stringify({ cwd: process.cwd(), args: process.argv.slice(2) }) : config.output || '');
    process.exit(config.exitCode || 0);
  }, config.delay);
} else {
  setInterval(() => {}, 1000);
}
