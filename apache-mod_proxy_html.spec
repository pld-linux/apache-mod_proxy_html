%define		mod_name	proxy_html
%define 	apxs		/usr/sbin/apxs

Summary:	mod_proxy_html - additional proxy module for rewriting HTML links
Summary(pl):	mod_proxy_html - dodatkowy modu³ proxy do przepisywania odno¶ników HTML
Name:		apache-mod_%{mod_name}
Version:	2.3
Release:	1
License:	GPL
Group:		Networking/Daemons
Source0:	http://apache.webthing.com/mod_proxy_html/mod_proxy_html.c
# Source0-md5:	b1211dff5343d75ac376ef836557f11f
URL:		http://apache.webthing.com/mod_proxy_html/
BuildRequires:	%{apxs}
BuildRequires:	apache-devel >= 2.0.44
BuildRequires:	apr-devel >= 1:0.9.4-1
BuildRequires:	autoconf
BuildRequires:	automake
BuildRequires:	libxml2-devel >= 2.5.10
Requires(post,preun):	%{apxs}
Requires:	apache >= 2.0.44
Requires:	apache-mod_proxy >= 2.0.44
Requires:	libxml2 >= 2.5.10
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%define		apache_moddir	%(%{apxs} -q LIBEXECDIR)

%description
mod_proxy_html is additional proxy module for rewriting HTML links
so that they don't break in a reverse proxy.

%description -l pl
mod_proxy_html to dodatkowy modu³ proxy do przepisywania
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
install -d $RPM_BUILD_ROOT{%{apache_moddir},/etc/httpd/httpd.conf}

install .libs/mod_%{mod_name}.so $RPM_BUILD_ROOT%{apache_moddir}
cat <<EOF > $RPM_BUILD_ROOT/etc/httpd/httpd.conf/35_mod_%{mod_name}.conf
LoadModule proxy_html_module    modules/mod_proxy_html.so

# You will find configuration instructions here:
# http://apache.webthing.com/mod_proxy_html/config.html
EOF

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ -f /var/lock/subsys/httpd ]; then
	/etc/rc.d/init.d/httpd restart 1>&2
fi

%preun
if [ "$1" = "0" ]; then
	if [ -f /var/lock/subsys/httpd ]; then
		/etc/rc.d/init.d/httpd restart 1>&2
	fi
fi

%files
%defattr(644,root,root,755)
%config(noreplace) %verify(not size mtime md5) /etc/httpd/httpd.conf/*
%attr(755,root,root) %{apache_moddir}/*
