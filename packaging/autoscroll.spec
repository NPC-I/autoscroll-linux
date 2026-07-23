Name:     autoscroll
Version:  1.0.0
Release:  1%{?dist}
Summary:  System-wide Windows-style middle-click autoscroll for Linux
License:  MIT
URL:      https://github.com/talos/autoscroll-linux
Source0:  %{name}-%{version}.tar.gz
BuildRequires: cmake >= 3.16
BuildRequires: gcc
BuildRequires: systemd-rpm-macros

%description
autoscroll provides system-wide middle-click autoscroll (like Windows)
for Linux. Works on X11 and all Wayland compositors.

%prep
%setup -q -n autoscroll-linux

%build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON \
      -DCMAKE_INSTALL_MANDIR=share/man \
      -DCMAKE_INSTALL_SYSCONFDIR=/etc .
cmake --build build

%install
cmake --install build --prefix %{buildroot}%{_prefix}
install -D -m 0644 service/autoscroll.service %{buildroot}/usr/lib/systemd/system/autoscroll.service
install -D -m 0644 udev/99-autoscroll.rules %{buildroot}/usr/lib/udev/rules.d/99-autoscroll.rules

%check
ctest --test-dir build

%post
%systemd_post autoscroll.service
udevadm control --reload-rules 2>/dev/null || :
udevadm trigger --subsystem-match=input 2>/dev/null || :

%preun
%systemd_preun autoscroll.service

%postun
%systemd_postun_with_restart autoscroll.service

%files
%{_bindir}/autoscroll
/usr/lib/systemd/system/autoscroll.service
/usr/lib/udev/rules.d/99-autoscroll.rules
%{_mandir}/man1/autoscroll.1*

%changelog
* Thu Jul 23 2026 autoscroll-linux contributors <root@localhost> - 1.0.0-1
- Initial release
