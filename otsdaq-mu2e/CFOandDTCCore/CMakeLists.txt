
cet_make_library(LIBRARY_NAME CFOandDTCCore
SOURCE CFOandDTCCoreVInterface.cc
		 LIBRARIES PUBLIC
		 mu2e-pcie-utils::CFOInterface
		 otsdaq-mu2e::ROCCore
		 otsdaq-artdaqinterface::ARTDAQTableBase
)

install_headers()
install_source()
