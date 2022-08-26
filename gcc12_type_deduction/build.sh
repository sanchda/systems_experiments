args="-std=c++20 test.cpp -o main"
ccs=(g++-10 g++-11 g++-12 clang++-13)
for cxx in "${ccs[@]}"; do
  echo ==$cxx==
  if ! eval $cxx "${args}"; then
    echo "Failed!"
  else
    echo "Success!"
  fi
done
#g++-12 -std=c++20 test.cpp -o main
#clang++-13 -std=c++20 test.cpp -o main
