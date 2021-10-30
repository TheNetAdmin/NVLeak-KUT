print_profiler() {
    echo \#\#\# Profiler: AEPWatch
    echo \#\#\#     - Bin:           $(which AEPWatch)
    echo
}

load_profiler() {
    source /opt/intel/aepwatch/aep_vars.sh
}

unload_profiler() {
    :
}

start_profiler() {
    if [ -z $TaskDir ]; then
        echo "[$0](start_profiler): Invalid output path"
        exit 1
    fi
    AEPWatch 1 -g -f $TaskDir/AEPWatch.csv & disown
    sleep 3
}

stop_profiler() {
    sleep 3
    AEPWatch-stop
}
