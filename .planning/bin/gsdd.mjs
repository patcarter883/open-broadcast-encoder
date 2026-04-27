#!/usr/bin/env node

import { cmdFileOp } from './lib/file-ops.mjs';
import { cmdLifecyclePreflight } from './lib/lifecycle-preflight.mjs';
import { cmdPhaseStatus } from './lib/phase.mjs';
import { bootstrapHelperWorkspace, consumeWorkspaceRootArg, resolveWorkspaceContext } from './lib/workspace-root.mjs';

const COMMANDS = {
  'file-op': cmdFileOp,
  'lifecycle-preflight': cmdLifecyclePreflight,
  'phase-status': cmdPhaseStatus,
};

function printHelp() {
  console.log([
    'Usage: node .planning/bin/gsdd.mjs [--workspace-root <path>] <command> [args]',
    '',
    'Local workflow helper commands:',
    '  file-op <copy|delete|regex-sub>',
    '                               Run deterministic workspace-confined file operations',
    '                               Example: node .planning/bin/gsdd.mjs file-op delete .planning/.continue-here.bak --missing ok',
    '  phase-status <N> <status>   Update ROADMAP.md phase status ([ ] / [-] / [x])',
    '                               Example: node .planning/bin/gsdd.mjs phase-status 1 done',
    '  lifecycle-preflight <surface> [phase]',
    '                               Inspect lifecycle gate results for a workflow surface',
    '                               Example: node .planning/bin/gsdd.mjs lifecycle-preflight verify 1 --expects-mutation phase-status',
    '',
    'Advanced option:',
    '  --workspace-root <path>     Override workspace root discovery before or after the subcommand',
  ].join('\n'));
}

function applyWorkspaceRootOverride(workspaceRootArg) {
  if (!workspaceRootArg) {
    bootstrapHelperWorkspace(import.meta.url);
    return true;
  }

  const context = resolveWorkspaceContext(['--workspace-root', workspaceRootArg]);
  if (context.invalid) {
    console.error(context.error);
    process.exitCode = 1;
    return false;
  }

  process.env.GSDD_WORKSPACE_ROOT = context.workspaceRoot;
  try {
    process.chdir(context.workspaceRoot);
  } catch {
    // best-effort: command handlers also resolve from GSDD_WORKSPACE_ROOT
  }
  return true;
}

async function main() {
  const parsed = consumeWorkspaceRootArg(process.argv.slice(2));
  if (parsed.invalid) {
    console.error('Usage: --workspace-root <path>');
    process.exitCode = 1;
    return;
  }

  if (!applyWorkspaceRootOverride(parsed.workspaceRootArg)) return;

  const [command, ...args] = parsed.args;

  if (!command || command === 'help' || command === '--help') {
    printHelp();
    return;
  }

  const handler = COMMANDS[command];
  if (!handler) {
    printHelp();
    process.exitCode = 1;
    return;
  }

  await handler(...args);
}

await main();
