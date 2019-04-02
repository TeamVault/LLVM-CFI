#!/bin/bash

if [ -z ${LLVM_CONFIG+x} ]; then
	echo "Need to set LLVM_CONFIG environment variable with path to config.py"
	exit 0
fi

if [ ! $($LLVM_CONFIG LLVM_DIR) ]; then
	echo "LLVM_CONFIG variable needs to be set to a valid path to config.py. config.py needs to be executable."
fi

# delete bundled icu to prevent problems with header (because using the systems icu)
# find third_party/icu -type f \! -regex '.*\.\(gyp\|gypi\|isolate\)' -delete

# create a empty testfile, because it is missing but needed by build
touch chrome/test/data/webui/i18n_process_css_test.html

# prepare nacl build for clang
#sed -i.bak 's#|| defined(__clang__)##g' native_client/src/trusted/service_runtime/arch/x86_64/nacl_syscall_64.S
#sed -i.bak 's#|| defined(__clang__)##g' native_client/src/trusted/service_runtime/arch/x86_64/nacl_switch_64.S


# fix bug in chromium gyp file

if [ -f build/gyp_chromium.bak ]; then
	mv build/gyp_chromium.bak build/gyp_chromium
fi
sed -i.bak -e '247,249d' build/gyp_chromium


# fix missing headerfile (?!)

if [ -f chrome/browser/storage_monitor/storage_monitor_linux.cc.bak ]; then
	mv chrome/browser/storage_monitor/storage_monitor_linux.cc.bak chrome/browser/storage_monitor/storage_monitor_linux.cc
fi
#sed -i.bak '28 a include <sys/stat.h>;' chrome/browser/storage_monitor/storage_monitor_linux.cc

# prepare common.gypi for IVT and CastShield

if [ -f build/common.gypi.bak ]; then
	mv build/common.gypi.bak build/common.gypi
fi

new_lines="
      ['OS==\"linux\" and clang==1 and sd_lto==1', {
        'cflags+': ['-flto'],
        'ldflags+': ['-flto', '-B$($LLVM_CONFIG BINUTILS_BUILD_DIR)/gold/', '-Wl,-plugin-opt=save-temps'],
      }],
      ['OS==\"linux\" and clang==1 and sd_llvmcfi==1', {
        'cflags+': ['-fsanitize=cfi-vcall'],
      }],
      ['OS==\"linux\" and clang==1 and sd_ivtbl==1', {
        'cflags+': ['-flto', '-femit-ivtbl'],
        'ldflags+': ['-flto', '-B$($LLVM_CONFIG BINUTILS_BUILD_DIR)/gold/',
          '-Wl,-plugin-opt=sd-ivtbl',
          '-L$($LLVM_CONFIG LLVM_DIR)/libdyncast'
         ],
        'libraries+' : ['$($LLVM_CONFIG LLVM_DIR)/libdyncast/libdyncast.a']
      }],
      ['OS==\"linux\" and clang==1 and sd_vtbl_checks==1', {
        'cflags+': ['-femit-vtbl-checks'],
      }],
      ['OS==\"linux\" and clang==1 and cast_checks==1', {
        'cflags+': ['-femit-cast-checks'],
      }],
      ['OS==\"linux\" and clang==1 and sd_use_37==1', {
          'cflags+': [
          '-Wno-tautological-compare',
          '-Wno-unused-local-typedef',
          '-Wno-absolute-value',
          '-Wno-inconsistent-missing-override',
          '-Wno-undefined-bool-conversion',
          '-Wno-pointer-bool-conversion',
          '-Wno-string-conversion',
          '-Wno-gcc-compat'],
      }],"

echo $new_lines
echo $new_lines > new_lines.txt
sed -i.bak '2072r new_lines.txt' build/common.gypi
rm new_lines.txt

new_lines=$'\
    # Clang stuff.\
    \'sd_lto%\': \'<(sd_lto)\',\
    \'sd_llvmcfi%\': \'<(sd_llvmcfi)\',\
    \'sd_use_37%\': \'<(sd_use_37)\',\
    \'sd_ivtbl%\': \'<(sd_ivtbl)\',\
    \'sd_vtbl_checks%\': \'<(sd_vtbl_checks)\',\
    \'cast_checks%\': \'<(cast_checks)\','

echo $new_lines > new_lines.txt
sed -i.bak2 "1093r new_lines.txt" build/common.gypi
rm new_lines.txt

new_lines=$'\
      # Set this to true when building with Clang + SafeDispatch only with CHA turned on.\
      \'sd_use_37\': 0,\
      \'sd_lto\': 0,\
      \'sd_llvmcfi\': 0,\
      \'sd_ivtbl%\': 0,\
      \'sd_vtbl_checks%\': 0,\
      \'cast_checks%\': 0,'

rm build/common.gypi.bak2

echo $new_lines > new_lines.txt
sed -i.bak2 "463r new_lines.txt" build/common.gypi
rm new_lines.txt
rm build/common.gypi.bak2
rm new_lines.txt

# if libdyncast does not exist we can make it...

if [ ! -f $($LLVM_CONFIG LLVM_DIR)/libdyncast/libdyncast.a ]; then
	pushd $($LLVM_CONFIG LLVM_DIR)/libdyncast >/dev/null
	make >/dev/null
	popd >/dev/null
fi

# if you source this file, ar will be automatically set...
export AR=$($LLVM_CONFIG LLVM_DIR)/scripts/ar

# symlink llvm into chromium source folder
ln -sf $($LLVM_CONFIG LLVM_BUILD_DIR) third_party/llvm-build
ln -sf $($LLVM_CONFIG LLVM_DIR) third_party/llvm

new_lines="
$($LLVM_CONFIG LLVM_BUILD_DIR)/Release+Asserts/bin/llc -filetype=obj \"\$obj_file\" -o=\"\${obj_file}.sd.o\"
\"\$bin_file\" \"\$asm_format\" \"\${obj_file}.sd.o\" > \"\$out_file\""

if [ -f third_party/libvpx/obj_int_extract.sh.bak ];then
	mv third_party/libvpx/obj_int_extract.sh.bak third_party/libvpx/obj_int_extract.sh
fi

sed -i.bak -e '30d' third_party/libvpx/obj_int_extract.sh
echo $new_lines >> third_party/libvpx/obj_int_extract.sh
rm new_lines.txt


# works with debian 9 

GYP_DEFINES='linux_use_gold_binary=0 linux_use_bunbled_binutils=0 linux_use_binutils_binary=0 linux_use_gold_flags=0 clang=1 sd_use_37=1 sd_lto=1 python_ver=2.7 use_nss_certs=0 use_openssl=1 use_openssl_certs=1 use_gnome_keyring=0 disable_nacl=1 disable_pnacl=1 use_gold=0 clang_use_chrome_plugins=0 use_llvm_37==1 werror= use_cups=0 use_pulseaudio=0'
# autoset GYP_DEFINES if argument is given
if [[ "$1" == "ivt" || "$1" == "cast_checks" || "$1" == "combi" ]]; then
	export GYP_DEFINES="$GYP_DEFINES sd_ivtbl=1"
fi

if [[ "$1" == "ivt" || "$1" == "combi" ]]; then
	export GYP_DEFINES="$GYP_DEFINES sd_vtbl_checks=1"
elif [[ "$1" == "cast_checks" || "$1" == "combi" ]]; then
	export GYP_DEFINES="$GYP_DEFINES cast_checks=1"
fi

# use chromiums unbundle to remove some build files

python build/linux/unbundle/replace_gyp_files.py -D$(echo $GYP_DEFINES | sed 's/ / -D/g')

# ensure we hav ld.gold
pushd $($LLVM_CONFIG BINUTILS_BUILD_DIR)/gold >/dev/null
ln -sf ld-new ld
popd >/dev/null

