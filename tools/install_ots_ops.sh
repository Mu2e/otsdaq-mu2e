#!/bin/bash

# ==========================================
# Argument Parsing
# ==========================================
if [ -z "$1" ]; then
    echo "Error: No target directory specified."
    echo "Usage: $0 <target_directory>"
    exit 1
fi

TARGET_DIR="$1"

# ==========================================
# Configuration Variables
# ==========================================
SPACK_SCRIPT_URL="https://raw.githubusercontent.com/Mu2e/otsdaq_mu2e/refs/heads/develop/tools/mu2e-quick-spack-start_v0.28.sh"
SPACK_SCRIPT_NAME="mu2e-quick-spack-start.sh"
CONFIG_REPO="git@github.com:Mu2e/otsdaq_mu2e_config.git"
SECRETS_DIR="$HOME/.secrets"

SUBSYSTEMS=("shift" "trigger" "cfo" "calo" "crv" "stm" "dcs" "dqm" "tracker")

# ==========================================
# Workspace Setup
# ==========================================
echo "Setting up workspace in: $TARGET_DIR"
if [ -d "$TARGET_DIR" ]; then
    echo "Error: Directory '$TARGET_DIR' already exists. Aborting to prevent overwriting."
    exit 1
fi

mkdir "$TARGET_DIR"
cd "$TARGET_DIR"

# ==========================================
# Repository Access Check (Run Before Spack)
# ==========================================
echo "Cloning otsdaq-mu2e-config..."
# Temporarily disable exit-on-error so we can catch the git clone failure
set +e
git clone "$CONFIG_REPO" otsdaq-mu2e-config
CLONE_STATUS=$?
set +e # Leave exit-on-error off; later commands may return non-zero benignly

if [ $CLONE_STATUS -eq 0 ]; then
    cd otsdaq-mu2e-config
    git fetch --all --quiet
    git checkout mu2e/ots_ops
    cd ..
fi

if [ $CLONE_STATUS -ne 0 ]; then
    echo ""
    echo "======================================================================"
    echo "ERROR: Could not clone the configuration repository."
    echo "This usually means you do not have the correct GitHub SSH key set up or lack access."
    echo ""
    echo "Please follow these steps to set up your SSH key:"
    echo "  1. cd ~/.ssh/"
    echo "  2. ssh-keygen  # Name your key (e.g., id_username_rsa) so multiple users can coexist."
    echo "                 # Note: For shared accounts, password-protecting your key is highly recommended!"
    echo "  3. cat id_username_rsa.pub"
    echo "  4. Copy the entire file output into GitHub (Settings -> SSH and GPG keys)."
    echo "  5. To start using GitHub without extra Mu2e script help, run:"
    echo "     export GIT_SSH_COMMAND=\"ssh -i ~/.ssh/id_username_rsa\""
    echo ""
    echo "Once completed, please delete the '$TARGET_DIR' directory and run this script again."
    echo "======================================================================"
    exit 1
fi

# ==========================================
# Spack Environment Setup
# ==========================================
echo "Downloading Spack start script..."
curl -L -s "$SPACK_SCRIPT_URL" -o "$SPACK_SCRIPT_NAME"
chmod +x "$SPACK_SCRIPT_NAME"

# Clear Spack root if it exists
unset SPACK_ROOT

echo "Running Spack setup (this may take a while)..."
(./"$SPACK_SCRIPT_NAME" --all-packages --develop --dev-only --artdaq --otsdaq)
if [ $? -ne 0 ]; then
    echo "ERROR: Spack setup failed!"
    exit 1
fi

# ==========================================
# Cleanup spack install artifacts
# ==========================================
echo "Cleaning up install artifacts..."
rm -f setup-env.sh setup_spack_build_system_v0.28.sh fonts.* mu2e-quick-spack-start.sh mu2e-quick-spack-start.lastrun.sh

# ==========================================
# Configuration and Secrets
# ==========================================
echo "Setting up OTS scripts..."
if [ -f "setup_ots.sh" ]; then
    mv setup_ots.sh old.setup_ots.sh
fi
ln -s otsdaq-mu2e-config/hwdev_spack_fast_setup_ots.sh setup_ots.sh
ln -s otsdaq-mu2e-config/setup_kinit.sh kinit_setup.sh
ln -s ../otsdaq-mu2e-config/status.sh srcs/status.sh

# Check for secrets, warn if missing
echo "Checking for database setup secrets..."
if [ -f "$SECRETS_DIR/db_setup_ots.sh" ] && [ -f "$SECRETS_DIR/mongodb_setup.sh" ]; then
    cp "$SECRETS_DIR/db_setup_ots.sh" ./
    cp "$SECRETS_DIR/mongodb_setup.sh" ./
    echo "Secrets copied successfully."
else
    echo "WARNING: Secrets not found in $SECRETS_DIR."
    echo "Please download the setup scripts manually from DocDB (50596):"
    echo "Link: https://mu2e-docdb.fnal.gov/cgi-bin/sso/RetrieveFile?docid=50596"
    read -p "Press Enter to continue once the files are in the current directory, or Ctrl+C to abort..."
fi

if [ -f "$SECRETS_DIR/ecl_setup_ots.sh" ]; then
    cp "$SECRETS_DIR/ecl_setup_ots.sh" ./
fi

# ==========================================
# Switch repos to mu2e/ots_ops and rebuild
# ==========================================
echo "Switching repos to mu2e/ots_ops where ahead of develop..."
cd srcs
bash status.sh --checkout
cd ..

echo "Rebuilding with mu2e/ots_ops changes..."
bash -ic "source setup_ots.sh cfo && mb"

# ==========================================
# Subsystem Processing
# ==========================================
for subsystem in "${SUBSYSTEMS[@]}"; do
    echo "----------------------------------------"
    echo "Processing subsystem: $subsystem"
    echo "----------------------------------------"

    USER_DATA="Data_$subsystem"
    mkdir -p "$USER_DATA/ServiceData/"

    # Copy and link configuration files
    cp otsdaq-mu2e-config/CoreTableInfoNames.dat "$USER_DATA/ServiceData/"
    cp "otsdaq-mu2e-config/Data_${subsystem}/ServiceData/ActiveTableGroups.cfg" "$USER_DATA/ServiceData/ActiveTableGroups.cfg"
    ln -sf "../../otsdaq-mu2e-config/webPortTranslation.dat" "$USER_DATA/ServiceData/webPortTranslation.dat"

    # Copy security.dat
    SECURITY_SRC="otsdaq-mu2e-config/Data_${subsystem}/ServiceData/OtsWizardData/security.dat"
    if [ -f "$SECURITY_SRC" ]; then
        mkdir -p "$USER_DATA/ServiceData/OtsWizardData/"
        cp "$SECURITY_SRC" "$USER_DATA/ServiceData/OtsWizardData/"
    fi

    # Copy login data for shift subsystem
    if [ "$subsystem" == "shift" ]; then
        mkdir -p "$USER_DATA/ServiceData/LoginData/HashesData/"
        mkdir -p "$USER_DATA/ServiceData/LoginData/UsersData/"
        cp "otsdaq-mu2e-config/Data_shift/ServiceData/LoginData/HashesData/hashes.xml" "$USER_DATA/ServiceData/LoginData/HashesData/" 2>/dev/null
        cp "otsdaq-mu2e-config/Data_shift/ServiceData/LoginData/UsersData/users.xml" "$USER_DATA/ServiceData/LoginData/UsersData/" 2>/dev/null
    fi

    # Execute the DAQ setup inside a subshell `(...)`
    # This ensures that sourced environment variables don't pollute the next iteration
    (
        source setup_ots.sh "$subsystem"
        UpdateOTS.sh --tables
        ots --wiz
        yes Y | ots -k
    )

    echo "Finished $subsystem."
done

echo "========================================"
echo "All subsystems processed successfully!"
echo "========================================"
