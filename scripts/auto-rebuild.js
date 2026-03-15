const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const log = (...args) => console.log('[electron-wns postinstall]', ...args);

const appRoot = process.env.INIT_CWD;

if (process.platform !== 'win32') {
  log(`Skipping rebuild: host platform ${process.platform} is not Windows.`);
  process.exit(0);
}

if (!appRoot || !fs.existsSync(path.join(appRoot, 'package.json'))) {
  log('Skipping rebuild: INIT_CWD is missing or invalid.');
  process.exit(0);
}

const electronPackageJson = path.join(appRoot, 'node_modules', 'electron', 'package.json');
if (!fs.existsSync(electronPackageJson)) {
  log('Skipping rebuild: electron is not installed in consumer project.');
  process.exit(0);
}

const cliCandidates = [
  path.join(appRoot, 'node_modules', '@electron', 'rebuild', 'lib', 'cli.js'),
  path.join(__dirname, '..', 'node_modules', '@electron', 'rebuild', 'lib', 'cli.js')
];

const rebuildCli = cliCandidates.find(candidate => fs.existsSync(candidate));
if (!rebuildCli) {
  log('Skipping rebuild: @electron/rebuild CLI not found.');
  process.exit(0);
}

log('Detected Electron project at:', appRoot);
log('Running:', `${process.execPath} ${rebuildCli} -f -w electron-wns`);

const result = spawnSync(process.execPath, [rebuildCli, '-f', '-w', 'electron-wns'], {
  cwd: appRoot,
  stdio: 'inherit',
  env: process.env
});

if (result.error) {
  console.error('[electron-wns postinstall] Failed to run electron-rebuild:', result.error.message);
  process.exit(1);
}

if (typeof result.status === 'number' && result.status !== 0) {
  process.exit(result.status);
}

log('electron-wns rebuild completed.');
process.exit(0);
