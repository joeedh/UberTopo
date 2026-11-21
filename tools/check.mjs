// The local check (docs/building.md): runs CMake workflow presets one after
// another under litestl's configureEnv.mjs and prints one line per preset.
//
//   node tools/check.mjs [--clean] [preset...]
//
// The default presets are clang-cl-rel and msvc-asan on Windows, clang-rel and
// gcc-asan elsewhere. --clean deletes each preset's build directory first;
// use it for a phase gate, since an incremental build only reports warnings
// for the files it recompiles. Output goes to build/<preset>.log. The exit
// status is non-zero if any preset fails or warns in UberTopo's sources.

import fs from 'fs'
import Path from 'path'
import child_process from 'child_process'
import {fileURLToPath} from 'url'

const ROOT = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)), '..')
const ENV_SCRIPT = Path.join(ROOT, 'source', 'litestl', 'configureEnv.mjs')
const DEFAULTS = process.platform === 'win32' ? ['clang-cl-rel', 'msvc-asan'] : ['clang-rel', 'gcc-asan']

const args = process.argv.slice(2)
const clean = args.includes('--clean')
const presets = args.filter((a) => !a.startsWith('--'))
if (presets.length === 0) {
  presets.push(...DEFAULTS)
}

// A warning located in UberTopo's own sources (not litestl's), as clang,
// clang-cl, gcc or cl print it. Each distinct line counts once, because a
// header's warning repeats in every translation unit that includes it.
const OWN_WARNING = /source[\\/]ubertopo[\\/].*\bwarning\b/

function runPreset(preset) {
  const bdir = Path.join(ROOT, 'build', preset)
  if (clean) {
    fs.rmSync(bdir, {recursive: true, force: true})
  }
  fs.mkdirSync(Path.join(ROOT, 'build'), {recursive: true})
  const logPath = Path.join(ROOT, 'build', `${preset}.log`)
  const started = Date.now()
  const r = child_process.spawnSync(
    process.execPath,
    [ENV_SCRIPT, 'cmake', '--workflow', '--preset', preset],
    {cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 30}
  )
  const log = (r.stdout || '') + (r.stderr || '')
  fs.writeFileSync(logPath, log)
  const lines = log.split(/\r?\n/)
  const warnings = new Set(lines.filter((l) => OWN_WARNING.test(l)))
  const summary = lines.find((l) => /tests passed/.test(l)) || 'no test summary (configure or build failed)'
  const ok = r.status === 0 && warnings.size === 0
  const secs = Math.round((Date.now() - started) / 1000)
  console.log(
    `${ok ? 'PASS' : 'FAIL'} ${preset}: ${summary.trim()}; ${warnings.size} UberTopo warning(s); ${secs}s` +
      (ok ? '' : ` (see ${Path.relative(ROOT, logPath)})`)
  )
  for (const w of [...warnings].slice(0, 10)) {
    console.log(`    ${w.trim()}`)
  }
  return ok
}

let allOk = true
for (const p of presets) {
  allOk = runPreset(p) && allOk
}
process.exit(allOk ? 0 : 1)
