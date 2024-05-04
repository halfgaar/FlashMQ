#!/bin/bash
set -eu

die() {
	>&2 echo "Fatal error: $*"
	exit 9
}

[[ $# -eq 2 ]] || die "Expected 2 arguments, got $#: $*
Usage: [ template file ] [ .dbk5 file ]"

template="$1"
dbk5="$2"

cat "$template"

# Finding config options
xmlnames=$(sed -n '/^\s*<varlistentry xml:id="\([^"]*\)">.*/ { s//\1/; p }' "$dbk5" | sort)
readarray -t all_config_names <<< "$xmlnames"

# This will contain listen, bridge etc.
declare -a GLOBAL_SCOPES=()

echo "\""
echo "\" Local scopes"
echo "\""
echo ""

for configname in "${all_config_names[@]}"
do
	# In general we'll add 2 regexes to match a directive:
	#
	#   [:space:]*directive[:space:]
	#   [:space:]*directive$
	#
	# The first form is how it's used in a full configuration file
	#
	# The second regex is nice while typing since it will
	# color the directive without having to press space first
	#
	if [[ "$configname" == *"__"* ]];
	then
		# this is a nested option like bridge__address
		scope=${configname%__*}  # bridge
		localname=${configname#*__}  # address
		echo "syn match fmq_${scope}_Directive \"^\s*$localname\s\" contained"
		echo "syn match fmq_${scope}_Directive \"^\s*$localname\$\" contained"
		GLOBAL_SCOPES+=("$scope")

	else
		# this is a global option like log_file
		echo "syn match fmqTopLevelDirective \"^\s*$configname\s\""
		echo "syn match fmqTopLevelDirective \"^\s*$configname\$\""
	fi
done

readarray -t unique_global_scopes <<< "$(echo "${GLOBAL_SCOPES[*]}" | sed 's/ /\n/g' | sort | uniq)"

echo ""
echo "\""
echo "\" Global scopes"
echo "\""
echo ""

for scope in "${unique_global_scopes[@]}"
do
	echo "syn region fmq_${scope}_InnerBlock start=\"{\" end=\"}\" contains=fmq_${scope}_Directive,fmqComment"
	echo "syn region fmq_${scope}_Block start=\"^$scope\s\" end=\"$\" contains=fmq_${scope}_InnerBlock"
	echo "syn region fmq_${scope}_Block start=\"^$scope\$\" end=\"$\""
	echo "hi link fmq_${scope}_Directive Type"
	echo "hi link fmq_${scope}_Block Statement"
	echo ""
done
