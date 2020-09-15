# Banker example

## Sw-tracing in the same style as COEMS hw-based tracing works:

````
$ make
$ ./run_one.sh -c examples/banker_lock.c
$ ./generate_banker_spec.sh
$ ./banker_lock race
Data race condition. Thread id '0' omits to lock mutex 'accts[4].mtxt'
...
Total money in system: 1000
$ perl diff_ts.pl <traces.log | java -jar /path/to/tessla.jar banker.spec  | grep in_line
3287064: error_1_in_line = 52
3326703: error_1_in_line = 53
3446018: error_1_in_line = 54
...
```

The final `print`-statement in the `banker`-example does not follow the locking-discipline
and we report a race-warning:

```
$ ./banker_lock
Total money in system: 1000
$ perl diff_ts.pl <traces.log | java -jar /path/to/tessla.jar banker.spec  | grep in_line
24576427: error_0_in_line = 96
24592123: error_1_in_line = 96
```

## Alternative "full" sw-tracing

Initial version of Eraser for NIK'18 paper. Produces more false positives.

```
$ java -jar /path/to/tessla.jar data-race-spec/full_lockset_tessla_1.1.0_varaddr.tessla  traces.log | grep in_line
646928: error_in_line = 28
994706: error_in_line = 28
1306904: error_in_line = 59
1630174: error_in_line = 41
```
