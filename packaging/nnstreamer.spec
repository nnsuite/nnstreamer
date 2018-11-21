# Execute gbs with --define "testcoverage 1" in case that you must get unittest coverage statictics
%define		gstpostfix	gstreamer-1.0
%define		gstlibdir	%{_libdir}/%{gstpostfix}

Name:		nnstreamer
Summary:	gstremaer plugins for neural networks
Version:	0.0.3
Release:	1rc1
# From 0.0.3, don't put letters as the first character of release. Use "1.rc1" instead of "rc1".
# We are using "stable-1" because we've been using "rc#" for 0.0.2
Group:		Applications/Multimedia
Packager:	MyungJoo Ham <myungjoo.ham@samsung.com>
License:	LGPL-2.1
Source0:	nnstreamer-%{version}.tar.gz
Source1001:	nnstreamer.manifest

Requires:	gstreamer >= 1.8.0
BuildRequires:	gstreamer-devel
BuildRequires:	gst-plugins-base-devel
BuildRequires:	glib2-devel
BuildRequires:	cmake

# To run test cases, we need gst plugins
BuildRequires:	gst-plugins-good
BuildRequires:	gst-plugins-good-extra
BuildRequires:	gst-plugins-base
# and gtest
BuildRequires:	gtest-devel
# a few test cases uses python
BuildRequires:	python
BuildRequires:	python-numpy
# Testcase requires bmp2png, which requires libpng
BuildRequires:  pkgconfig(libpng)
# for tensorflow-lite
BuildRequires: tensorflow-lite-devel
# for cairo (nnstreamer_example_object_detection)
BuildRequires: coregl-devel
BuildRequires: cairo-devel
# custom_example_opencv filter requires opencv-devel
BuildRequires: opencv-devel
# For './testAll.sh' time limit.
BuildRequires: procps

%if 0%{?testcoverage}
BuildRequires: lcov
# BuildRequires:	taos-ci-unittest-coverage-assessment
%endif

# Unit Testing Uses SSAT (hhtps://github.com/myungjoo/SSAT.git)
BuildRequires: ssat

%package unittest-coverage
Summary:	NNStreamer UnitTest Coverage Analysis Result
%description unittest-coverage
HTML pages of lcov results of NNStreamer generated during rpmbuild

%description
NNStreamer is a set of gstreamer plugins to support general neural networks
and their plugins in a gstreamer stream.

%package devel
Summary:	Development package for custom tensor operator developers (tensor_filter/custom)
Requires:	nnstreamer = %{version}-%{release}
Requires:	glib2-devel
Requires:	gstreamer-devel
%description devel
Development package for custom tensor operator developers (tensor_filter/custom).
This contains corresponding header files and .pc pkgconfig file.

%package example
Summary:	NNStreamer example custom plugins and test plugins
Requires:	nnstreamer = %{version}-%{release}
%description example
Example custom tensor_filter subplugins and
plugins created for test purpose.


%prep
%setup -q
cp %{SOURCE1001} .

%build
%if 0%{?testcoverage}
CXXFLAGS="${CXXFLAGS} -fprofile-arcs -ftest-coverage"
CFLAGS="${CFLAGS} -fprofile-arcs -ftest-coverage"
%endif

mkdir -p build
pushd build
export GST_PLUGIN_PATH=$(pwd)
export LD_LIBRARY_PATH=$(pwd):$(pwd)/gst/tensor_filter
%cmake .. -DGST_INSTALL_DIR=%{gstlibdir} -DENABLE_MODEL_DOWNLOAD=OFF
make %{?_smp_mflags}
%if 0%{?unit_test}
    ./tests/unittest_common
    ./tests/unittest_sink --gst-plugin-path=.
    ./tests/unittest_plugins --gst-plugin-path=.
    popd
    pushd tests
    ssat
%endif
popd

%install
pushd build
%make_install
popd

%if 0%{?testcoverage}
##
# The included directories are:
#
# gst: the nnstreamer elements
# nnstreamer_example: custom plugin examples
# common: common libraries for gst (elements)
# include: common library headers and headers for external code (packaged as "devel")
#
# Intentionally excluded directories are:
#
# tests: We are not going to show testcoverage of the test code itself or example applications
    $(pwd)/tests/unittestcoverage.py module $(pwd)/gst

# Get commit info
    VCS=`cat ${RPM_SOURCE_DIR}/nnstreamer.spec | grep "^VCS:" | sed "s|VCS:\\W*\\(.*\\)|\\1|"`

# Create human readable unit test coverate report web page
    # Create null gcda files if gcov didn't create it because there is completely no unit test for them.
    find . -name "*.gcno" -exec sh -c 'touch -a "${1%.gcno}.gcda"' _ {} \;
    # Remove gcda for meaningless file (CMake's autogenerated)
    find . -name "CMakeCCompilerId*.gcda" -delete
    find . -name "CMakeCXXCompilerId*.gcda" -delete
    #find . -path "/build/*.j
    # Generate report
    lcov -t 'NNStreamer Unit Test Coverage' -o unittest.info -c -d . -b $(pwd) --no-external
    # Visualize the report
    genhtml -o result unittest.info -t "nnstreamer %{version}-%{release} ${VCS}" --ignore-errors source -p ${RPM_BUILD_DIR}
%endif

%if 0%{?testcoverage}
mkdir -p %{buildroot}%{_datadir}/nnstreamer/unittest/
cp -r result %{buildroot}%{_datadir}/nnstreamer/unittest/
%endif

install build/libnnstreamer.a %{buildroot}%{_libdir}/
install build/gst/tensor_filter/*.a %{buildroot}%{_libdir}/

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%manifest nnstreamer.manifest
%defattr(-,root,root,-)
%license LICENSE
%{gstlibdir}/*.so

%files devel
%{_includedir}/nnstreamer/*
%{_libdir}/*.a
%{_libdir}/pkgconfig/nnstreamer.pc

%if 0%{?testcoverage}
%files unittest-coverage
%{_datadir}/nnstreamer/unittest/*
%endif

%files example
%manifest nnstreamer.manifest
%defattr(-,root,root,-)
%license LICENSE
%{_libdir}/*.so


%changelog
* Mon Oct 15 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
- Started single-binary packaging for 0.0.3

* Fri May 25 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
- Packaged tensor_convert plugin.
