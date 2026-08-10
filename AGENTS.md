# Remote environment operating notes

## File transfer policy

- Do not transfer payloads, repositories, archives, packages, models, or other large files to or from the remote Ascend environment through SSH. This prohibition includes `scp`, `sftp`, `rsync` over SSH, piping archives or binary data into `ssh`, and base64/chunked SSH transfer. The SSH path is too slow and is reserved for commands, diagnostics, and interactive administration. A later explicit user authorization permits a narrow exception for tiny credential and control-script files; never use that exception for payload data.
- When a file must be transferred, upload it from A to a GitHub Release asset, then download it from the remote environment through B's HTTP proxy. Prefer `gh` or `gh api` for GitHub Release and Issue operations; do not use browser automation when the CLI/API covers the operation.
- The repository synchronization workflow is an intentional exception to the GitHub Release default: it uses the user's private Guangzhou OSS bucket because measured Release-asset throughput is extremely poor. It overwrites the fixed object key `gh/mozhijiawei/vllm-stack-yellow-zone/remote-sync/latest.bundle`; do not create timestamped bundle objects.
- Repository source should normally be fetched directly from GitHub with Git. A GitHub source archive is an acceptable bootstrap fallback when Git is not yet available.
- Before installing or changing remote software, collect the relevant system, network, package, service, and configuration state. Do not assume a standard Linux or Windows image.

## Default repository synchronization path

- Use the OSS-backed Git bundle workflow as the default and only routine path for updating code on the Ascend environment. Do not use `git clone`/`git pull` from the Ascend host and do not send repository data over SSH.
- Run the workflow from repository root on A after the desired changes have been committed and pushed to `origin/main`:

  ```powershell
  Set-ExecutionPolicy -Scope Process Bypass -Force
  .\scripts\remote-sync\Publish-AscendBundle.ps1
  ```

- The publisher fetches the latest `origin/main`; local uncommitted changes and commits that have not been pushed are deliberately excluded. It builds a complete bundle, verifies it, and overwrites the single private OSS object `gh/mozhijiawei/vllm-stack-yellow-zone/remote-sync/latest.bundle`.
- The publisher then sends only the expiring signed URL and control data over SSH to `root@9.15.144.34`. The Ascend host downloads the payload directly from OSS and creates or fast-forwards `/root/l00933108/vllm-stack-yellow-zone`.
- A successful run must complete the remote bundle verification and report the resulting remote commit. Treat a dirty remote worktree, a non-fast-forward update, fetch failure, upload failure, download failure, or verification failure as a stop condition; diagnose it rather than bypassing the guard.
- If the remote controller or protected OSS credential file is missing after the Ascend environment is reinstalled, bootstrap them once from A:

  ```powershell
  Set-ExecutionPolicy -Scope Process Bypass -Force
  .\scripts\remote-sync\Install-AscendRemoteSync.ps1
  ```

- The installer may use the explicitly approved small-file SSH exception only for `/root/l00933108/bin/update-code-from-bundle.sh` and `/root/l00933108/.secrets/gh-oss-attachments.env`. All repository payload still travels directly through OSS.
- Do not modify the OSS bucket configuration, permissions, lifecycle, or versioning for this workflow. Do not create timestamped bundle objects, list the bucket, or delete objects. Every publication must replace the fixed `latest.bundle` key.
- Defaults and supported overrides are documented in `scripts/remote-sync/README.md`. Keep that document and this section aligned whenever the workflow changes.

## Network topology and verified endpoints

- A is the local Windows machine running Codex.
- B is the Windows bridge machine. Its Tailscale address is `100.103.138.57`, hostname `desktop-s9tb764`, and its LAN address toward the Ascend environment is `19.3.25.108`.
- B's observed public egress is `121.37.53.201`, a Huawei Cloud Guangzhou address. Prefer Guangzhou or nearby mainland-China storage and mirrors when choosing a download source.
- The current Ascend environment is `9.15.144.34`.
- Tailscale route `9.15.144.34/32` is advertised by B and approved. The obsolete approved route `9.15.154.38/32` was removed.
- A can reach TCP port 22 on `9.15.144.34` through B. ICMP may time out and is not a reliable health check for this path.
- Root public-key SSH from A is verified: `ssh root@9.15.144.34`. Root password login remains disabled (`PermitRootLogin without-password`).

## B-side HTTP proxy

- B runs the `VllmStackPrivoxy` Windows service.
- The proxy listens on `http://19.3.25.108:18080` and permits the Ascend client `9.15.144.34`.
- The service command loads `C:\ProgramData\VllmStackBridge\privoxy\bridge.conf`.
- End-to-end HTTPS access from the Ascend environment through the proxy has been verified against Baidu and GitHub.

## Current Ascend environment state

### Container lifecycle policy

- Treat `cont1_ljw` and `cont2_ljw` as long-lived, fixed experiment containers. Reuse them for remote experiments whenever their image and device mapping are compatible.
- Repository code is bind-mounted into those containers. Update the remote repository through the OSS bundle workflow and use the mounted checkout; do not rebuild or restart containers merely to update source code.
- Recreate a container only when an image, device mapping, mount, or other container-creation-time dependency cannot be changed in place. Collect state and explain that requirement before replacement.

- OS: EulerOS 2.0 SP13, architecture `aarch64`.
- Use `/root/l00933108` as the single remote working directory for tools, repositories, downloads, and build work.
- Persistent shell proxy configuration is `/etc/profile.d/vllm-stack-proxy.sh`, exporting upper- and lower-case HTTP/HTTPS proxy variables for `http://19.3.25.108:18080`.
- Missing CA certificate symlinks were repaired. `/etc/pki/tls/cert.pem` and `/etc/pki/tls/certs/ca-bundle.crt` point to `/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem`.
- The image is minimal. It has `rpm`, `curl`, `tar`, `gzip`, `xz`, `python3`, `pkg-config`, and `openssl`, but no `yum`, `dnf`, `microdnf`, compiler, or Make.
- Git 2.55.0 is installed at `/usr/local/bin/git`. Its isolated runtime is `/root/l00933108/.tools/git`, installed with micromamba from the TUNA conda-forge aarch64 mirror.
- The pair-scheduler native-container baseline image is available as `quay.io/ascend/vllm-ascend:v0.19.1rc1`, manifest digest `sha256:66fd1ee885ffa696e79b1cd6034d4d6a4b1bec121b3c1cec9b596ad298362caa`. It was pulled through the digest-identical Nanjing mirror and tagged with the official name.
- The pinned xLite wheel is `/root/l00933108/deps/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl`, SHA-256 `cccb74688f6acb9cc219290c3a04b6005b81dba941b9d63c79bd52d02854fc8a`.
- The system `/usr/local/bin/ctr` is policy-restricted. Pair-scheduler scripts must use the private upstream client at `/root/l00933108/.tools/containerd/bin/ctr`; do not replace the system binary.
- The pair-scheduler preparation path uses native Ascend containers and models under `/cache/models`. It intentionally has no dependency on vCANN-RT, GDB, `enpu-monitor`, `npu_info.config`, or a custom `ld.so.preload`.
- A complete GitHub source archive was successfully downloaded through B at about 10.4 MiB/s (approximately 87 Mbit/s), confirming that large GitHub downloads work.
- The validated micromamba aarch64 bootstrap archive is `/root/l00933108/.tools/micromamba-linux-aarch64.tar.bz2`, SHA-256 `e705ffeed90ce0659eb546e4b1e1028c9eaf0bc9cc854867b19ac5ce0ba5852f`.
- Direct GitHub Smart HTTP cloning is currently unreliable through B: ten clone attempts failed with proxy 503 or TLS EOF before object transfer. Do not repeatedly retry this path without changing the download route.
- Measured source performance through B: Huawei Cloud Ubuntu mirror about 26.7 MiB/s, TUNA Ubuntu mirror about 9.7 MiB/s, TUNA conda-forge repodata about 4.8 MiB/s, GitHub codeload about 10.4 MiB/s, and GitHub Release asset objects only about 0.01 MiB/s. Prefer Huawei Cloud mirrors for general large files and TUNA for conda-forge packages. Avoid GitHub Release assets for large transfers when a faster trusted source is available.
- Alibaba OSS public regional endpoints are reachable. Observed first-byte times were approximately Guangzhou 0.23 s, Shenzhen 0.35 s, Beijing 0.39 s, Hangzhou 0.44 s, Chengdu 0.55 s, and Shanghai 0.78 s. Prefer an `oss-cn-guangzhou` bucket for this environment when possible, but benchmark an actual bucket object before relying on it for large transfers.
- The SSH server does not provide an SCP/SFTP subsystem. This is not a problem because SSH file transfer is prohibited by the policy above.
