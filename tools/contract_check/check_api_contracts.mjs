#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tsTypesPath = path.join(root, '.archive', 'v1', 'web', 'src', 'api', 'types.ts');

const messageContracts = [
  {
    name: 'ServerState',
    proto: 'proto/caster/core/ServerState.proto',
  },
  {
    name: 'ClientState',
    proto: 'proto/caster/core/ClientState.proto',
  },
  {
    name: 'StreamState',
    proto: 'proto/caster/core/StreamState.proto',
  },
  {
    name: 'SourceRecord',
    proto: 'proto/caster/core/SourceRecord.proto',
    allowProtoOnly: {
      ecef_x: 'Web source records do not display or write back ECEF coordinates yet.',
      ecef_y: 'Web source records do not display or write back ECEF coordinates yet.',
      ecef_z: 'Web source records do not display or write back ECEF coordinates yet.',
    },
  },
  {
    name: 'AccountRecord',
    proto: 'proto/caster/auth/AccountRecord.proto',
  },
  {
    name: 'AccountActive',
    proto: 'proto/caster/auth/AccountActive.proto',
    allowTsOnly: {
      connect_key: 'NC-008A/B ACT:SESSION uses the Redis hash field as the connection identity.',
      anonymous: 'NC-008A/B ACT:SESSION display JSON extension not reflected in proto yet.',
      auth_type: 'NC-008A/B ACT:SESSION display JSON extension not reflected in proto yet.',
      group_uid: 'NC-008A/B ACT:SESSION display JSON extension not reflected in proto yet.',
    },
  },
  {
    name: 'AliasRule',
    proto: 'proto/caster/core/AliasRule.proto',
  },
  {
    name: 'AccessGroup',
    proto: 'proto/caster/core/AccessGroup.proto',
  },
  {
    name: 'AccessItem',
    proto: 'proto/caster/core/AccessItem.proto',
  },
  {
    name: 'PullRecord',
    proto: 'proto/caster/core/PullRecord.proto',
  },
  {
    name: 'PullState',
    proto: 'proto/caster/core/PullState.proto',
  },
  {
    name: 'PushRecord',
    proto: 'proto/caster/core/PushRecord.proto',
  },
  {
    name: 'PushState',
    proto: 'proto/caster/core/PushState.proto',
  },
  {
    name: 'CasterNode',
    proto: 'proto/caster/core/CasterNode.proto',
  },
];

const enumContracts = [
  {
    name: 'PullType',
    proto: 'proto/caster/Common.proto',
  },
  {
    name: 'PushType',
    proto: 'proto/caster/Common.proto',
  },
  {
    name: 'SourceRecordType',
    proto: 'proto/caster/Common.proto',
  },
  {
    name: 'SourceDecordType',
    proto: 'proto/caster/Common.proto',
  },
  {
    name: 'SourceDisplayType',
    proto: 'proto/caster/Common.proto',
  },
  {
    name: 'AccountType',
    proto: 'proto/caster/auth/AccountRecord.proto',
  },
  {
    name: 'AccountStateType',
    proto: 'proto/caster/auth/AccountRecord.proto',
  },
  {
    name: 'AccountActiveState',
    proto: 'proto/caster/auth/AccountRecord.proto',
  },
  {
    name: 'AccessState',
    proto: 'proto/caster/core/AccessItem.proto',
  },
];

function readText(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8');
}

function stripComments(text) {
  return text
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/.*$/gm, '');
}

function extractBlock(text, keyword, name) {
  const startPattern = new RegExp(`\\b${keyword}\\s+${name}\\s*{`, 'm');
  const startMatch = startPattern.exec(text);
  if (!startMatch) {
    return null;
  }

  let depth = 0;
  const blockStart = startMatch.index + startMatch[0].length;
  for (let index = startMatch.index; index < text.length; index += 1) {
    const char = text[index];
    if (char === '{') {
      depth += 1;
    } else if (char === '}') {
      depth -= 1;
      if (depth === 0) {
        return text.slice(blockStart, index);
      }
    }
  }

  return null;
}

function parseProtoMessageFields(relativePath, name) {
  const block = extractBlock(stripComments(readText(relativePath)), 'message', name);
  if (block === null) {
    throw new Error(`Missing proto message ${name} in ${relativePath}`);
  }

  return Array.from(block.matchAll(/^\s*(?:optional\s+|repeated\s+)?(?:[.A-Za-z_][A-Za-z0-9_.<>]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=/gm))
    .map((match) => match[1]);
}

function parseProtoEnumValues(relativePath, name) {
  const block = extractBlock(stripComments(readText(relativePath)), 'enum', name);
  if (block === null) {
    throw new Error(`Missing proto enum ${name} in ${relativePath}`);
  }

  return Array.from(block.matchAll(/^\s*([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)/gm))
    .map((match) => ({ name: match[1], value: Number(match[2]) }));
}

function parseTsInterfaceFields(text, name) {
  const block = extractBlock(stripComments(text), 'interface', name);
  if (block === null) {
    throw new Error(`Missing TypeScript interface ${name} in .archive/v1/web/src/api/types.ts`);
  }

  return Array.from(block.matchAll(/^\s*([A-Za-z_][A-Za-z0-9_]*)\??\s*:/gm))
    .map((match) => match[1]);
}

function parseTsEnumValues(text, name) {
  const block = extractBlock(stripComments(text), 'enum', name);
  if (block === null) {
    throw new Error(`Missing TypeScript enum ${name} in .archive/v1/web/src/api/types.ts`);
  }

  return Array.from(block.matchAll(/^\s*([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)/gm))
    .map((match) => ({ name: match[1], value: Number(match[2]) }));
}

function diffFields(left, right) {
  const rightSet = new Set(right);
  return left.filter((item) => !rightSet.has(item));
}

function reportAllowed(kind, contractName, fields, allowMap) {
  if (fields.length === 0) {
    return;
  }

  console.log(`[allowed] ${contractName} ${kind}:`);
  for (const field of fields) {
    console.log(`  - ${field}: ${allowMap[field]}`);
  }
}

function checkCollection({ label, contracts, protoParser, tsParser, protoOnlyLabel, tsOnlyLabel }) {
  const failures = [];
  const tsText = fs.readFileSync(tsTypesPath, 'utf8');

  console.log(`\n[contract-check] ${label}`);
  for (const contract of contracts) {
    const protoValues = protoParser(contract.proto, contract.name);
    const tsValues = tsParser(tsText, contract.name);

    const protoOnly = diffFields(protoValues, tsValues);
    const tsOnly = diffFields(tsValues, protoValues);
    const allowedProtoOnly = protoOnly.filter((field) => contract.allowProtoOnly?.[field]);
    const allowedTsOnly = tsOnly.filter((field) => contract.allowTsOnly?.[field]);
    const unexpectedProtoOnly = protoOnly.filter((field) => !contract.allowProtoOnly?.[field]);
    const unexpectedTsOnly = tsOnly.filter((field) => !contract.allowTsOnly?.[field]);

    if (unexpectedProtoOnly.length || unexpectedTsOnly.length) {
      failures.push({
        contract,
        unexpectedProtoOnly,
        unexpectedTsOnly,
      });
    }

    reportAllowed(protoOnlyLabel, contract.name, allowedProtoOnly, contract.allowProtoOnly ?? {});
    reportAllowed(tsOnlyLabel, contract.name, allowedTsOnly, contract.allowTsOnly ?? {});

    if (!protoOnly.length && !tsOnly.length) {
      console.log(`[ok] ${contract.name}`);
    } else if (!unexpectedProtoOnly.length && !unexpectedTsOnly.length) {
      console.log(`[ok] ${contract.name} has documented drift only`);
    }
  }

  return failures;
}

function printFailures(label, failures, protoOnlyLabel, tsOnlyLabel) {
  if (failures.length === 0) {
    return;
  }

  console.error(`\n[contract-check] ${label} failed`);
  for (const failure of failures) {
    console.error(`- ${failure.contract.name} (${failure.contract.proto})`);
    if (failure.unexpectedProtoOnly.length) {
      console.error(`  unexpected ${protoOnlyLabel}: ${failure.unexpectedProtoOnly.join(', ')}`);
    }
    if (failure.unexpectedTsOnly.length) {
      console.error(`  unexpected ${tsOnlyLabel}: ${failure.unexpectedTsOnly.join(', ')}`);
    }
  }
}

function checkEnumContracts() {
  const failures = [];
  const tsText = fs.readFileSync(tsTypesPath, 'utf8');

  console.log('\n[contract-check] enum values');
  for (const contract of enumContracts) {
    const protoValues = parseProtoEnumValues(contract.proto, contract.name);
    const tsValues = parseTsEnumValues(tsText, contract.name);
    const protoMap = new Map(protoValues.map((entry) => [entry.name, entry.value]));
    const tsMap = new Map(tsValues.map((entry) => [entry.name, entry.value]));
    const protoOnly = protoValues.filter((entry) => !tsMap.has(entry.name)).map((entry) => entry.name);
    const tsOnly = tsValues.filter((entry) => !protoMap.has(entry.name)).map((entry) => entry.name);
    const valueMismatches = protoValues
      .filter((entry) => tsMap.has(entry.name) && tsMap.get(entry.name) !== entry.value)
      .map((entry) => ({
        name: entry.name,
        protoValue: entry.value,
        tsValue: tsMap.get(entry.name),
      }));

    if (protoOnly.length || tsOnly.length || valueMismatches.length) {
      failures.push({
        contract,
        protoOnly,
        tsOnly,
        valueMismatches,
      });
    } else {
      console.log(`[ok] ${contract.name}`);
    }
  }

  return failures;
}

function printEnumFailures(failures) {
  if (failures.length === 0) {
    return;
  }

  console.error('\n[contract-check] enum values failed');
  for (const failure of failures) {
    console.error(`- ${failure.contract.name} (${failure.contract.proto})`);
    if (failure.protoOnly.length) {
      console.error(`  unexpected proto-only values: ${failure.protoOnly.join(', ')}`);
    }
    if (failure.tsOnly.length) {
      console.error(`  unexpected ts-only values: ${failure.tsOnly.join(', ')}`);
    }
    for (const mismatch of failure.valueMismatches) {
      console.error(`  value mismatch ${mismatch.name}: proto=${mismatch.protoValue} ts=${mismatch.tsValue}`);
    }
  }
}

const messageFailures = checkCollection({
  label: 'message fields',
  contracts: messageContracts,
  protoParser: parseProtoMessageFields,
  tsParser: parseTsInterfaceFields,
  protoOnlyLabel: 'proto-only fields',
  tsOnlyLabel: 'ts-only fields',
});

const enumFailures = checkEnumContracts();

printFailures('message fields', messageFailures, 'proto-only fields', 'ts-only fields');
printEnumFailures(enumFailures);

if (messageFailures.length || enumFailures.length) {
  console.error('\n[contract-check] FAIL: update proto, .archive/v1/web/src/api/types.ts, or the documented allowlist.');
  process.exit(1);
}

console.log('\n[contract-check] PASS: proto message fields/enums and Web API types are in sync.');
