g++ -O3 -funroll-loops -march=native -mtune=generic  lplibs/LPThreadpool.cpp  lplibs/LPSparseArrayGeneric.h lplibs/LPSparseBitset.cpp lplibs/LPBitset.cpp  main.cpp -lpthread -o mainSigmod2014 -w
