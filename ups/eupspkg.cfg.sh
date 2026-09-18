build() {
NJOBS="${EUPSPKG_NJOBS:-1}"

mkdir build
cd build
cmake -S .. -B .
cmake --build . --config Release -j $NJOBS
}
