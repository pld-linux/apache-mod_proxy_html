# TODO
# - update to apache 2.2 (build fails)
%define		mod_name	proxy_html
%define 	apxs		/usr/sbin/apxs
Summary:	mod_proxy_html - additional proxy module for rewriting HTML links
Summary(pl.UTF-8):	mod_proxy_html - dodatkowy moduł proxy do przepisywania odnośników HTML
Name:		apache-mod_%{mod_name}
Version:	2.3
Release:	1
License:	GPL
Group:		Networking/Daemons/HTTP
Source0:	http://apache.webthing.com/mod_proxy_html/mod_proxy_html.c
# Source0-md5:	b1211dff5343d75ac376ef836557f11f
URL:		http://apache.webthing.com/mod_proxy_html/
BuildRequires:	%{apxs}
BuildRequires:	apache-devel >= 2.0.44
BuildRequires:	apr-devel >= 1:0.9.4-1
BuildRequires:	autoconf
BuildRequires:	automake
BuildRequires:	libxml2-devel >= 2.5.10
BuildRequires:	rpmbuild(macros) >= 1.268
Requires:	apache(modules-api) = %apache_modules_api
Requires:	apache-mod_proxy
Requires:	libxml2 >= 2.5.10
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%define		_pkglibdir	%(%{apxs} -q LIBEXECDIR 2>/dev/null)
%define		_sysconfdir	%(%{apxs} -q SYSCONFDIR 2>/dev/null)

%description
mod_proxy_html is additional proxy module for rewriting HTML links so
that they don't break in a reverse proxy.

%description -l pl.UTF-8
mod_proxy_html to dodatkowy moduł proxy do przepisywania odnośników
HTML w ten sposób, by nie były uszkadzane przez odwrotne proxy.

%prep
%setup -q -c -T
cp %{SOURCE0} .

%build
%{apxs} \
	-c -o mod_%{mod_name}.la \
	$(%{_bindir}/xml2-config --cflags --libs) \
	mod_%{mod_name}.c

%install
rm -rf $RPM_BUILD_ROOT
install -d $RPM_BUILD_ROOT{%{_pkglibdir},%{_sysconfdir}/httpd.conf}

install .libs/mod_%{mod_name}.so $RPM_BUILD_ROOT%{_pkglibdir}
cat <<EOF > $RPM_BUILD_ROOT%{_sysconfdir}/httpd.conf/35_mod_%{mod_name}.conf
LoadModule proxy_html_module	modules/mod_proxy_html.so

# You will find configuration instructions here:
# http://apache.webthing.com/mod_proxy_html/config.html
EOF

%clean
rm -rf $RPM_BUILD_ROOT

%post
%service -q httpd restart

%postun
if [ "$1" = "0" ]; then
	%service -q httpd restart
fi

%files
%defattr(644,root,root,755)
%attr(640,root,root) %config(noreplace) %verify(not md5 mtime size) %{_sysconfdir}/httpd.conf/*_mod_%{mod_name}.conf
%attr(755,root,root) %{_pkglibdir}/*.so
