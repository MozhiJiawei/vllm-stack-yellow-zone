# Ascend repository synchronization

This workflow updates the isolated Ascend environment without transferring repository payloads through SSH and without depending on unreliable GitHub Smart HTTP access from B.

## Design

1. A fetches `origin/main` from GitHub.
2. A creates a complete Git bundle containing the committed `origin/main` history.
3. A uploads the bundle to a fixed `remote-sync/latest.bundle` object in the configured private Aliyun OSS bucket with resumable multipart upload, replacing the previous completed bundle, and creates a seven-day V4 signed GET URL.
4. Only the signed URL and control output travel through SSH.
5. The Ascend environment downloads the bundle directly from OSS, verifies it with Git, and creates or fast-forwards `/root/l00933108/vllm-stack-yellow-zone`.

Local uncommitted changes are never included. The remote updater refuses to touch a dirty worktree and refuses non-fast-forward updates.

## One-time installation

Run from an A PowerShell prompt:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\scripts\remote-sync\Install-AscendRemoteSync.ps1
```

This installs two small protected files through the explicitly authorized SSH exception:

- `/root/l00933108/.secrets/gh-oss-attachments.env`, mode `600`;
- `/root/l00933108/bin/update-code-from-bundle.sh`, mode `700`.

The credential file is never printed, committed, or uploaded.

## Publish and update

Run from an A PowerShell prompt whenever committed `origin/main` should be deployed:

```powershell
.\scripts\remote-sync\Publish-AscendBundle.ps1
```

The script uses:

- local repository: the repository containing this script;
- OSS config: `C:\Users\37274\.secrets\gh-oss-attachments.env`;
- remote host: `root@9.15.144.34`;
- remote worktree: `/root/l00933108/vllm-stack-yellow-zone`.

All values can be overridden with PowerShell parameters. The OSS object remains private; the generated signed URL expires after seven days.

The object key is stable (`gh/mozhijiawei/vllm-stack-yellow-zone/remote-sync/latest.bundle` with the default configuration), so each successful publish replaces the previous bundle instead of creating another timestamped object. The workflow does not list or delete OSS objects and does not require lifecycle, versioning, ListBucket, or DeleteObject permissions.
