Group: 59028709, 58992077
Members:
  SEITZHANOV Islam (iseitzhan2, ID: 59028709)
  KONTAYEV Dauren (dkontayev2, ID: 58992077)

How to compile:
  cd CS3103
  make all

This will produce three executables:
  ./pipeline   - Q1: Order Compression Pipeline
  ./warehouse  - Q2: Warehouse Directory
  ./scheduler  - Q3: Order Scheduling

How to run:
  ./pipeline <P> <M> <N> <num_orders> <T> <cnt_0>...<cnt_{T-1}> <tA_0> <tB_0>...
  ./warehouse <input_file>
  ./scheduler <input_file>

Example:
  ./pipeline 3 4 4 90 3 2 2 2 0 1 1 2 0 2
  ./warehouse test_cases/q2/case0.txt
  ./scheduler test_cases/q3/case0.txt

Known limitations:
  - None. All four test cases pass for each question.

Notes: 
  - Been making project with previous requirements and some parts are just 
  changed by AI suggestion, logic and architecture is working good.
