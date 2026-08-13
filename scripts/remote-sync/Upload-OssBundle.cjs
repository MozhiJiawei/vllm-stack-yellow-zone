#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const OSS = require("ali-oss");

const MAX_EXPIRES_SECONDS = 604800;

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const token = argv[index];
    if (!token.startsWith("--")) throw new Error(`Unexpected argument: ${token}`);
    const name = token.slice(2);
    const value = argv[index + 1];
    if (!value || value.startsWith("--")) throw new Error(`Missing value for --${name}`);
    args[name] = value;
    index += 1;
  }
  return args;
}

function parseEnvFile(filePath) {
  const values = {};
  for (const rawLine of fs.readFileSync(filePath, "utf8").split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const equals = line.indexOf("=");
    if (equals < 1) continue;
    const name = line.slice(0, equals).trim();
    let value = line.slice(equals + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    }
    values[name] = value;
  }
  return values;
}

function required(values, name) {
  if (!values[name]) throw new Error(`Missing required OSS configuration: ${name}`);
  return values[name];
}

function safeSegment(value) {
  return String(value)
    .toLowerCase()
    .replace(/[^a-z0-9._/-]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/^[-/]+|[-/]+$/g, "") || "repository";
}

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const configPath = path.resolve(required(args, "config"));
  const filePath = path.resolve(required(args, "file"));
  const repo = safeSegment(required(args, "repo"));
  const env = parseEnvFile(configPath);
  const prefix = required(env, "ALIYUN_OSS_PREFIX").replace(/^\/+/, "").replace(/\/?$/, "/");
  if (!prefix.startsWith("gh/")) throw new Error("OSS prefix must start with gh/.");

  const expires = Number(args.expires || env.ALIYUN_OSS_SIGN_EXPIRES_SECONDS || 604800);
  if (!Number.isInteger(expires) || expires < 1 || expires > MAX_EXPIRES_SECONDS) {
    throw new Error(`Expiration must be between 1 and ${MAX_EXPIRES_SECONDS} seconds.`);
  }

  const regionValue = required(env, "ALIYUN_OSS_REGION");
  const region = regionValue.startsWith("oss-") ? regionValue : `oss-${regionValue}`;
  const endpoint = required(env, "ALIYUN_OSS_ENDPOINT");
  const client = new OSS({
    accessKeyId: required(env, "ALIYUN_OSS_ACCESS_KEY_ID"),
    accessKeySecret: required(env, "ALIYUN_OSS_ACCESS_KEY_SECRET"),
    bucket: required(env, "ALIYUN_OSS_BUCKET"),
    endpoint,
    region,
    authorizationV4: true,
    secure: endpoint.startsWith("https://"),
    timeout: 30000,
  });

  const objectKey = `${prefix}${repo}/remote-sync/latest.bundle`;
  const totalBytes = fs.statSync(filePath).size;
  let checkpoint;
  let result;

  for (let attempt = 1; attempt <= 4; attempt += 1) {
    try {
      result = await client.multipartUpload(objectKey, filePath, {
        checkpoint,
        parallel: 4,
        partSize: 5 * 1024 * 1024,
        headers: { "Content-Type": "application/octet-stream" },
        progress(percentage, currentCheckpoint) {
          checkpoint = currentCheckpoint;
          process.stderr.write(
            `OSS_UPLOAD attempt=${attempt} progress=${(percentage * 100).toFixed(1)}% bytes=${totalBytes}\n`,
          );
        },
      });
      break;
    } catch (error) {
      if (attempt === 4) throw error;
      process.stderr.write(`OSS_UPLOAD_RETRY attempt=${attempt} error=${error.code || error.name}\n`);
      await sleep(attempt * 2000);
    }
  }

  if (!result) throw new Error("OSS multipart upload produced no result.");
  const signedUrl = await client.signatureUrlV4("GET", expires, undefined, objectKey);
  process.stdout.write(`${JSON.stringify({
    key: objectKey,
    size: totalBytes,
    signedUrl,
    expiresAt: new Date(Date.now() + expires * 1000).toISOString(),
  })}\n`);
}

main().catch((error) => {
  process.stderr.write(`OSS_UPLOAD_FAILED ${error.code || error.name}: ${error.message}\n`);
  process.exitCode = 1;
});
