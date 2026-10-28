#!/usr/bin/env bash
# Upload an ARC/RBF and its companion files to the SD root, then launch it.

main() {
    if [[ ${1:-} == --help || ${1:-} == -h ]]; then
        usage
        return 0
    fi
    if [[ $# -lt 2 ]]; then
        usage >&2
        return 2
    fi
    local host=$1 core=$2 file name rom_count=0
    shift 2
    local -a files=("$core" "$@") remote_names=()
    local netrc_file=${FTP_NETRC:-$HOME/.netrc}
    local -a curl_options=(--silent --show-error --fail --connect-timeout 10 --max-time 180)
    if [[ ! $host =~ ^[a-zA-Z0-9.-]+(:[0-9]+)?$ ]]; then
        echo 'HOST must be a hostname or IPv4 address, optionally with a port.' >&2
        return 2
    fi
    case ${core,,} in
        *.arc|*.rbf) ;;
        *) echo 'CORE must be an .arc or .rbf file.' >&2; return 2 ;;
    esac
    for file in "${files[@]}"; do
        check_file "$file" || return
        name=${file##*/}
        remote_names+=("$name")
        if [[ ${name,,} == *.rom ]]; then rom_count=$((rom_count + 1)); fi
    done
    if [[ ${core,,} == *.rbf && $rom_count -gt 1 ]]; then
        echo 'Use an ARC to select a game when supplying multiple .ROM files.' >&2
        return 2
    fi
    if [[ ${core,,} == *.rbf && $rom_count -eq 1 ]]; then
        echo 'Direct RBF loading: use an ARC when the game needs module/DIP/display settings.' >&2
    fi
    if [[ ! -r $netrc_file ]]; then
        echo "FTP credentials file not found or unreadable: $netrc_file" >&2
        echo "Add 'machine $host login USER password PASSWORD' to ~/.netrc (chmod 600)," >&2
        echo 'or set FTP_NETRC to your existing credentials file.' >&2
        return 2
    fi
    curl_options+=(--netrc-file "$netrc_file")
    name=${core##*/}
    local i remote
    # Direct RBF loading looks for <core>.ROM. Keep local game filenames intact.
    for i in "${!files[@]}"; do
        remote=${remote_names[i]}
        if [[ ${core,,} == *.rbf && ${remote,,} == *.rom ]]; then
            remote=${name%.*}.ROM
        fi
        echo "Uploading ${files[i]} -> /$remote"
        upload_file "$host" "${files[i]}" "$remote" "${curl_options[@]}" || return
    done
    echo "Loading /$name on $host"
    curl "${curl_options[@]}" --quote "SITE LOAD /$name" \
        --list-only --output /dev/null "ftp://$host/" || return
    echo "Loaded $name on $host"
}

usage() {
    echo "Usage: $0 HOST CORE.arc|CORE.rbf [ROM_OR_OTHER_FILE ...]"
    echo 'Local paths are relative to the directory where you run this command.'
    echo 'Files are uploaded to the SD root; with an RBF, a single ROM becomes <rbf-name>.ROM.'
    echo 'Use an ARC for game-specific settings or a different internal core name.'
    echo 'Credentials: ~/.netrc, or set FTP_NETRC to another netrc file.'
}

check_file() {
    local file=$1 parent
    if [[ $file == *$'\r'* || $file == *$'\n'* ]]; then
        echo 'Filenames containing line breaks are not supported.' >&2
        return 2
    fi
    if [[ -f $file && -r $file ]]; then return 0; fi
    echo "Local file not found or unreadable: $file" >&2
    echo "Working directory: $PWD" >&2
    parent=$file
    while [[ $parent == */* ]]; do
        parent=${parent%/*}
        [[ -n $parent ]] || break
        if [[ -e $parent && ! -d $parent ]]; then
            echo "Path component is a file, not a directory: $parent" >&2
            break
        fi
    done
    return 2
}

upload_file() {
    local host=$1 file=$2 name=$3 encoded='' char code i
    local LC_ALL=C
    shift 3
    # Explicit remote names need URL escaping, including filenames with spaces.
    for ((i = 0; i < ${#name}; i++)); do
        char=${name:i:1}
        case $char in
            [a-zA-Z0-9.~_-]) encoded+=$char ;;
            *) printf -v code '%%%02X' "'$char"; encoded+=$code ;;
        esac
    done
    curl "$@" --upload-file "$file" "ftp://$host/$encoded"
}

main "$@"
