#!/usr/bin/env bash

##
# @file config-plugins-audit.sh
# @brief add plugin modules for a github repository
# @see      https://github.com/nnsuite/TAOS-CI
# @author   Geunsik Lim <geunsik.lim@samsung.com>

##### Set environment for audit plugins
declare -i idx=-1

###### plugins-base ###############################################################################################
echo "[MODULE] plugins-base: Plugin group that is maintained with good quality"
# Please append your plugin modules here.

audit_plugins[++idx]="pr-audit-build-tizen"
echo "[DEBUG] The default BUILD_MODE of ${audit_plugins[idx]} is declared with 99 (SKIP MODE) by default in plugins-base folder."
echo "[DEBUG] ${audit_plugins[idx]} is started."
echo "[DEBUG] TAOS/${audit_plugins[idx]}: Check if Tizen rpm package is successfully generated."
echo "[DEBUG] Current path: $(pwd)."
source ${REFERENCE_REPOSITORY}/ci/taos/plugins-base/${audit_plugins[idx]}.sh
# Note that do not append the below "$module_name" because build step is implemented as a built-in module partially


audit_plugins[++idx]="pr-audit-build-ubuntu"
echo "[DEBUG] The default BUILD_MODE of ${audit_plugins[idx]} is declared with 99 (SKIP MODE) by default in plugins-base folder."
echo "[DEBUG] ${audit_plugins[idx]} is started."
echo "[DEBUG] TAOS/${audit_plugins[idx]}: Check if Ubuntu deb package is successfully generated."
echo "[DEBUG] Current path: $(pwd)."
source ${REFERENCE_REPOSITORY}/ci/taos/plugins-base/${audit_plugins[idx]}.sh
# Note that do not append the below "$module_name" because build step is implemented as a built-in module partially


#audit_plugins[++idx]="pr-audit-build-yocto"
#echo "[DEBUG] The default BUILD_MODE of ${audit_plugins[idx]} is declared with 99 by default in plugins-base folder."
#echo "[DEBUG] ${audit_plugins[idx]} is started."
#echo "[DEBUG] TAOS/${audit_plugins[idx]}: Check if YOCTO deb package is successfully generated."
#echo "[DEBUG] Current path: $(pwd)."
#source ${REFERENCE_REPOSITORY}/ci/taos/plugins-base/${audit_plugins[idx]}.sh
# Note that do not append the below "$module_name" because build step is implemented as a built-in module partially



###### plugins-good ###############################################################################################
echo "[MODULE] plugins-base: Plugin group that is maintained with good quality"
# Please append your plugin modules here.






###### plugins-staging ################################################################################################
echo "[MODULE] plugins-staging: Plugin group that does not have evaluation and aging test enough"
# Please append your plugin modules here.

# module_name="pr-audit-resource"
# echo "[DEBUG] $module_name is started."
# echo "[DEBUG] TAOS/$module_name: Check if there are not-installed resource files."
# echo "[DEBUG] Current path: $(pwd)."
# source ${REFERENCE_REPOSITORY}/ci/taos/plugins-staging/$module_name.sh
# $module_name
# echo "[DEBUG] $module_name is done."

