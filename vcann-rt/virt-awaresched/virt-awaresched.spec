%global version    1.0.0
%global release_version 2
%global __strip /bin/true

Name:       ubs-virt
Version:    1.0.0
Release:    2
Summary:    ubs-virt
License:    MulanPSL2
Source0:    %{name}.tar.gz
Provides:      %{name}
BuildRoot:     %{buildroot}
ExclusiveArch: %arm64

BuildRequires:  gcc-c++ gcc cmake make
BuildRequires:  patch libvirt-devel libboundscheck
Requires:       libvirt
Requires:       libboundscheck
Requires:       libcap

buildArch     : aarch64
ExclusiveArch : aarch64

%description
ubs-virt

%define service_name vas-daemon.service
%define project_dir %{_builddir}/%{name}/virt-awaresched
%define debug_package %{nil}

%package virt-awaresched
Summary: virt-awaresched
%description virt-awaresched
virt-awaresched build

%prep
%setup -q -T -b 0 -c -n ubs-virt

%build
#build virt-awaresched
if cd virt-awaresched; then
    if ! bash build.sh; then
        echo "[ERROR] Failed to build virt-awaresched"
        exit 1
    fi
else
    echo "[ERROR] Failed to change directory to virt-awaresched"
    exit 1
fi

%install
#install virt-awaresched
mkdir -p %{buildroot}/var/log/vas
mkdir -p %{buildroot}/usr/local/vas/bin
mkdir -p %{buildroot}/usr/local/bin
mkdir -p %{buildroot}/var/run/vas
ls -l  %{buildroot}/var/run
cp %{project_dir}/build/bin/vas_daemon %{buildroot}/usr/local/vas/bin/
cp %{project_dir}/build/bin/vasctl %{buildroot}/usr/local/bin/
mkdir -p %{buildroot}/usr/lib/systemd/system
cp %{project_dir}/%{service_name} %{buildroot}/usr/lib/systemd/system/

%pre
set -e
if systemctl cat %{service_name} >/dev/null 2>&1 ; then
    systemctl stop %{service_name} || true
    systemctl disable %{service_name} || true
fi

%post
set -e
systemctl daemon-reload
systemctl enable vas-daemon.service
if ! systemctl is-active --quiet vas-daemon.service; then
    systemctl start vas-daemon.service
fi

%preun
set -e
if [ "$1" -ne 0 ]; then
    echo "skip preun"
    exit 0
fi
if systemctl cat %{service_name} >/dev/null 2>&1 ; then
    systemctl stop %{service_name} || true
    systemctl disable %{service_name} || true
fi
if systemctl list-units --type=service | grep -q %{service_name}; then
    systemctl reset-failed %{service_name} || true
fi
service_file="/etc/systemd/system/vas-daemon.service"
if [ -f "$service_file" ]; then
    rm -rf $service_file
fi
systemctl daemon-reload

%postun
if [ "$1" -ne 0 ]; then
    echo "skip preun"
    exit 0
fi
if [ -d /usr/local/vas ]; then
    rm -rf /usr/local/vas
fi
if [ -d /var/run/vas ]; then
    rm -rf /var/run/vas
fi

%files virt-awaresched
%attr(0550, root, root) %dir /usr/local/vas
%attr(0500, root, root) %dir /usr/local/vas/bin
%attr(0750, root, root) %dir /var/log/vas
%attr(0700, root, root) %dir /var/run/vas
%attr(0500, root, root) /usr/local/vas/bin/vas_daemon
%attr(0500, root, root) /usr/local/bin/vasctl
%attr(0644, root, root) /usr/lib/systemd/system/vas-daemon.service

%changelog
* Fri Apr 24 2026 Zeren Lu <luzeren@h-partners.com> - 1.0.0-2
- Package init

* Fri Apr 24 2026 Zeren Lu <luzeren@h-partners.com> - 1.0.0-1
- 