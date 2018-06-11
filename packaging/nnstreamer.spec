# Execute gbs with --define "testcoverage 1" in case that you must get unittest coverage statictics

Name:		nnstreamer
Summary:	gstremaer plugins for neural networks
Version:	0.0.1
Release:	1
Group:		Applications/Multimedia
Packager:	MyungJoo Ham <myungjoo.ham@samsung.com>
License:	LGPL-2.1+ and Apache-2.0
Source0:	nnstreamer-%{version}.tar.gz
Source1001:	nnstreamer.manifest
Source2001:	testcase_tensor_converter.tar.gz

Requires:	gstreamer >= 1.8.0
Requires:	libdlog
BuildRequires:	gstreamer-devel
BuildRequires:	gst-plugins-base-devel
BuildRequires:	glib2-devel
BuildRequires:	cmake
BuildRequires:	libdlog-devel

# To run test cases, we need gst plugins
BuildRequires:	gst-plugins-good
BuildRequires:	gst-plugins-good-extra
BuildRequires:	gst-plugins-base
# and gtest
BuildRequires:	gtest-devel

%if 0%{?testcoverage}
BuildRequires:	taos-ci-unittest-coverage-assessment
%endif

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
%description devel
Development package for custom tensor operator developers (tensor_filter/custom).
This contains corresponding header files and .pc pkgconfig file.

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
%cmake .. -DTIZEN=ON
make %{?_smp_mflags}
popd

# DO THE TEST!

pushd build
./unittest_common
popd

pushd tensor_converter/test
# We skip testcase gen because it requires PIL, which requires tk.
# Use the pre-generated test cases
tar -xf %{SOURCE2001}
./runTest.sh -skipgen
popd

pushd tests/nnstreamer_filter_custom/
./runTest.sh
popd

%if 0%{?testcoverage}
    unittestcoverage.py module $(pwd)

# Get commit info
    VCS=`cat ${RPM_SOURCE_DIR}/audri.spec | grep "^VCS:" | sed "s|VCS:\\W*\\(.*\\)|\\1|"`                                              
                                                                                                                                       
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
    genhtml -o result unittest.info -t "AuDri %{version}-%{release} ${VCS}" --ignore-errors source -p ${RPM_BUILD_DIR}   
%endif

%install
pushd build
%make_install
popd

%if 0%{?testcoverage}
mkdir -p %{buildroot}%{_datadir}/nnstreamer/unittest/
cp -r result %{buildroot}%{_datadir}/nnstreamer/unittest/
%endif

%files
%manifest nnstreamer.manifest
%defattr(-,root,root,-)
# The libraries are in LGPLv2.1 (testcases and non GST-plugin components are APL2)
%license LICENSE.LGPLv2.1
%{_libdir}/*.so
%{_libdir}/*.so*
# TODO generate .so files with version info. Migrate symbolic-link .so to devel.

%files devel
%{_includedir}/nnstreamer/*
%{_libdir}/pkgconfig/nnstreamer.pc

%if 0%{?testcoverage}
%files unittest-coverage
%{_datadir}/nnstreamer/unittest/*
%endif

%changelog
* Fri May 25 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
- Packaged tensor_convert plugin.
