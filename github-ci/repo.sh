
if [[ -z $DEVLINE ]]; then
    echo "ERROR: an environment variable DEVLINE needs to be set for repo.sh to source properly"
    echo "Allowed values are \"develop\" and \"production_v4\""
    echo "It's possible you're running a script which has fallen out of maintenance; please contact John Freeman if you wish to use it"
    return 1
fi

if [[ "$DEVLINE" != "develop" && "$DEVLINE" != "production_v4" ]]; then
    echo "ERROR: environment variable DEVLINE set to an unexpected value \"$DEVLINE\""
    return 2
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

packages_with_ci=(
    "artdaq-core-mu2e"
    "artdaq-mu2e"
    "mu2e-pcie-utils"
    "mu2e-trig-config"
    "otsdaq-mu2e"
    "otsdaq-mu2e-calorimeter"
    "otsdaq-mu2e-crv"
    "otsdaq-mu2e-dqm"
    "otsdaq-mu2e-extmon"
    "otsdaq-mu2e-stm"
    "otsdaq-mu2e-sync"
    "otsdaq-mu2e-tracker"
    "otsdaq-mu2e-trigger"
)

packages_without_ci=(
  ".github"
  "daq-operations"
  "daq-docker"
  "otsdaq-mu2e-config"
  "TDAQFirmware"
  "mu2e-spack"
  "mu2e-tdaq-suite"
  "mu2edaq-shifter-tools"
  "mu2edaq-documentation"
)

packages=(
  "${packages_with_ci[@]}"
  "${packages_without_ci[@]}"
)
