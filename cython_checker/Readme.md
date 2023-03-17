# cython_checker

At the time of writing, some Python dependencies have been defective for a while because of the version of Cython they were built with (https://github.com/cython/cython/pull/4735).


To only show baddies
```
./check.sh <your_site-packages>
```

To show everything
```
./check.sh <your_site-packages> --showgood
```
