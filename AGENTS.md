# Remote environment operating notes

## File transfer policy

- Do not transfer payloads, repositories, archives, packages, models, or other large files to or from the remote Ascend environment through SSH. This prohibition includes `scp`, `sftp`, `rsync` over SSH, piping archives or binary data into `ssh`, and base64/chunked SSH transfer. The SSH path is too slow and is reserved for commands, diagnostics, and interactive administration. A later explicit user authorization permits a narrow exception for tiny credential and control-script files; never use that exception for payload data.
- When a file must be transferred, upload it from A to a GitHub Release asset, then download it from the remote environment through B's HTTP proxy. Prefer `gh` or `gh api` for GitHub Release and Issue operations; do not use browser automation when the CLI/API covers the operation.
- Repository source should normally be fetched directly from GitHub with Git. A GitHub source archive is an acceptable bootstrap fallback when Git is not yet available.
- Before installing or changing remote software, collect the relevant system, network, package, service, and configuration state. Do not assume a standard Linux or Windows image.

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

- OS: EulerOS 2.0 SP13, architecture `aarch64`.
- Use `/root/l00933108` as the single remote working directory for tools, repositories, downloads, and build work.
- Persistent shell proxy configuration is `/etc/profile.d/vllm-stack-proxy.sh`, exporting upper- and lower-case HTTP/HTTPS proxy variables for `http://19.3.25.108:18080`.
- Missing CA certificate symlinks were repaired. `/etc/pki/tls/cert.pem` and `/etc/pki/tls/certs/ca-bundle.crt` point to `/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem`.
- The image is minimal. It has `rpm`, `curl`, `tar`, `gzip`, `xz`, `python3`, `pkg-config`, and `openssl`, but no `yum`, `dnf`, `microdnf`, compiler, or Make.
- Git 2.55.0 is installed at `/usr/local/bin/git`. Its isolated runtime is `/root/l00933108/.tools/git`, installed with micromamba from the TUNA conda-forge aarch64 mirror.
- A complete GitHub source archive was successfully downloaded through B at about 10.4 MiB/s (approximately 87 Mbit/s), confirming that large GitHub downloads work.
- The validated micromamba aarch64 bootstrap archive is `/root/l00933108/.tools/micromamba-linux-aarch64.tar.bz2`, SHA-256 `e705ffeed90ce0659eb546e4b1e1028c9eaf0bc9cc854867b19ac5ce0ba5852f`.
- Direct GitHub Smart HTTP cloning is currently unreliable through B: ten clone attempts failed with proxy 503 or TLS EOF before object transfer. Do not repeatedly retry this path without changing the download route.
- Measured source performance through B: Huawei Cloud Ubuntu mirror about 26.7 MiB/s, TUNA Ubuntu mirror about 9.7 MiB/s, TUNA conda-forge repodata about 4.8 MiB/s, GitHub codeload about 10.4 MiB/s, and GitHub Release asset objects only about 0.01 MiB/s. Prefer Huawei Cloud mirrors for general large files and TUNA for conda-forge packages. Avoid GitHub Release assets for large transfers when a faster trusted source is available.
- Alibaba OSS public regional endpoints are reachable. Observed first-byte times were approximately Guangzhou 0.23 s, Shenzhen 0.35 s, Beijing 0.39 s, Hangzhou 0.44 s, Chengdu 0.55 s, and Shanghai 0.78 s. Prefer an `oss-cn-guangzhou` bucket for this environment when possible, but benchmark an actual bucket object before relying on it for large transfers.
- The SSH server does not provide an SCP/SFTP subsystem. This is not a problem because SSH file transfer is prohibited by the policy above.
