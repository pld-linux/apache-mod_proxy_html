%define		mod_name	proxy_html
%define 	apxs		/usr/sbin/apxs

Summary:	An embedded Python interpreter for the Apache Web server
Name:		apache-mod_%{mod_name}
Version:	0.20031204
Release:	1
License:	GPL
Group:		Networking/Daemons
# http://apache.webthing.com/mod_proxy_html/mod_proxy_html.c
Source0:	mod_proxy_html.c
Source1:	mod_proxy_html.html
URL:		http://apache.webthing.com/mod_proxy_html/
BuildRequires:	%{apxs}
BuildRequires:	apache-devel >= 2.0.44
BuildRequires:	apr-devel >= 1:0.9.4-1
BuildRequires:	autoconf
BuildRequires:	automake
BuildRequires:	libxml2-devel
Requires(post,preun):	%{apxs}
Requires:	apache >= 2.0.44
Requires:	apache-mod_proxy >= 2.0.44
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%define		apache_moddir	%(%{apxs} -q LIBEXECDIR)

%description
mod_proxy_html is additional proxy module for rewriting HTML links
so that they don't break in a reverse proxy.

%prep
%setup -q -c -T

%build
%{apxs} $(%{_bindir}/xml2-config --cflags --libs) $((%{_bindir}/apr-config --cflags --link-ld; %{_bindir}/apu-config --link-ld) | sed -e 's#-pthread#-lpthread#g') -c %{SOURCE0} -o mod_%{mod_name}.so

%install
rm -rf $RPM_BUILD_ROOT
install -d $RPM_BUILD_ROOT%{apache_moddir}

install mod_%{mod_name}.so $RPM_BUILD_ROOT%{apache_moddir}

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
%doc %{SOURCE1}
%attr(755,root,root) %{apache_moddir}/*
