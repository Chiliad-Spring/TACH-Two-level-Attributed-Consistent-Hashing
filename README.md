# TACH-Two-level-Attributed-Consistent-Hashing
## Basic installation

```
./prepare 
./configure --prefix=/installation path CFLAGS="-fopenmp" 
make -j 3
make install
make check # optional
```

## Run method

```
./src/ch-placement-benchmark      -s <number of servers>
                                  -d <number of devices>
                                  -o <number of objects>
                                  -r <replication factor>
                                  -v <virtual nodes per physical node>
                                  -b <size of block (KB)>
                                  -e <size of sector>
                                  -t <number of threads>
                                  -a <placement algorithm(1=TACH 2=Capacity-based 3=Performance-based 4=CH)>

```                                  
