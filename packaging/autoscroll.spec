Name:     autoscroll
Version:  1.0.0
Release:  1%{?dist}
Summary:  System-wide Windows-style middle-click autoscroll for Linux
License:  MIT
URL:      https://github.com/talos/autoscroll-linux
BuildRequires: cmake >= 3.16
BuildRequires: gcc

%description
autoscroll provides system-wide middle-click autoscroll (like Windows)
for Linux. Works on X11 and all Wayland compositors.

%prep
%setup -q

%build
%cmake -DBUILD_TESTS=ON
%cmake_build

%install
%cmake_install
install -D -m 0644 service/autoscroll.service %{buildroot}%{_unitdir}/autoscroll.service
install -D -m 0644 udev/99-autoscroll.rules %{buildroot}%{_udevrulesdir}/99-autoscroll.rules
install -D -m 0644 man/autoscroll.1 %{buildroot}%{_mandir}/man1/autoscroll.1

%post
%systemd_post autoscroll.service

%preun
%systemd_preun autoscroll.service

%files
%{_bindir}/autoscroll
%{_unitdir}/autoscroll.service
%{_udevrulesdir}/99-autoscroll.rules
%{_mandir}/man1/autoscroll.1*
%config(noreplace) /etc/autoscroll.conf

%changelog
* Thu Jul 23 2026 autoscroll-linux contributors <root@localhost> - 1.0.0-1
- Initial release
