=================================
Syntax support in Cider
=================================

Having syntax support
-----------------------------------

Generally, there are two cases in having. 
One is non-agg condition(See Case1 below) which will be regarded as filter operator and pushed down before group by partial agg. 

Case1:

.. code-block:: sql

        SELECT col_a, SUM(col_a) AS sum_a FROM test_table GROUP BY col_a HAVING col_a > 2;

The other one is agg condition(See Case2 below) which should be handled after group by final agg.

Case2:

.. code-block:: sql

    	SELECT col_a, SUM(col_a) AS sum_a FROM test_table GROUP BY col_a HAVING SUM(col_a) > 2;

For Case1 in Cider, we will get substrait plan in which having clause is transfered to filter operator already.
When it comes to Case3 that contains multiple conditions, we will receive a substrait plan with multiple
conditions. Then Cider will merge all those conditions and push them down before group by partial agg.

Case3:

.. code-block:: sql

        SELECT col_a, SUM(col_a) AS sum_a FROM test_table WHERE col_a < 10 GROUP BY col_a HAVING col_a > 2;

For Case2 in Cider, we expect to get two plans. One is table scan and partial agg, the other is final agg, filter and project.
So when it comes to Case4, two different conditions from where and having won't appear in a same substrait plan and be merged
into single EU.

Case4:

.. code-block:: sql

    	SELECT col_a, SUM(col_a) AS sum_a FROM test_table WHERE col_a < 10 GROUP BY col_a HAVING SUM(col_a) > 2;

In addition to those above, if we get an unexpected substrait plan like putting having agg condition together with partial
agg plan, we will get wrong result batch without throwing exception.


In syntax support
-----------------------------------

The IN clause allows multi values definition in WHERE conditions. For example:

.. code-block:: sql

        SELECT column_name(s)
        FROM table_name
        WHERE column_name IN (value1,value2,...);

Under this scenario, user translates IN expression to a substrait `ScalaFunction <https://github.com/substrait-io/substrait/blob/b8fb06a52397463bfe9cffc2c89fe71eba56b2ca/proto/substrait/algebra.proto#L387>`_ with `List <https://github.com/substrait-io/substrait/blob/b8fb06a52397463bfe9cffc2c89fe71eba56b2ca/proto/substrait/algebra.proto#L501>`_ as its second arg. Then Cider translates it into Analyzer::InValues for further codegen and computation.

IN can also be used together with a subquery:

.. code-block:: sql

        SELECT eno
        FROM employee
        WHERE dno IN
              (SELECT dno
              FROM dept
              WHERE floor = 3);

In this case, plan parser in frontend framework will parse it either "IN (value1, value2, ...)" or a JoinNode
when 'eno' col is known as a primary key or an unique index, like following:

.. code-block:: sql

        SELECT eno
        FROM employee join dept
        WHERE employee.dno = dept.dno and dept.floor = 3;

Thus this IN clause is handled through join op in Cider.

AVG support in Cider
-----------------------------------

Similar as other aggregation functions, 'AVG' has 2 phases(Partial/Final) in distributing data analytic engines. But computation is different in different phase. In AVG partial, computation is split into sum() and count() on target column/expression and in AVG final, sum() is done on previous summation and count value, then do a divide between these 2 values.

Since Cider is positioned as a compute library under such a distributed engine at task level, it doesn't support AVG syntax directly in its internal.

It may have some conflictions when frontend framework offloads AVG function to Cider, mainly caused by different signature of referred functions, such as output type, etc. Take Velox for example, it specifies **sum(int)** with output type **double** in avg aggregation, while it violates rules in cider which uses output type **bigint**. This will cause codegen check failure. So for this case, we made a workaround by following Cider rules in internal and convert result to **double** when retriving result into CiderBatch, thus can keep consistent schema with following op in velox plan, such as avg final computation.

Similar special handle will be needed when output type of agg functions from frontend framework violates with cider internal. In cider, the returned data types defined as following:

.. list-table

::

   :widths: 10 30
   :align: left
   :header-rows: 1
   * - Aggregate Function
     - Output Type
   * - SUM
     - If argument is integer, output type will be BIGINT. Otherwise same as argument type.
   * - MIN
     - Same as argument type.
   * - MAX
     - Same as argument type.
   * - COUNT
     - If g_bigint_count is true(default false), output type is BIGINT. Otherwise uses INT.


String Function support in Cider
-----------------------------------
Currently, Cider do not distinguish empty string and null string.

1) Like function
^^^^^^^^^^^^^^^^^^^^

a. Acceptable wildcards: %, _, []
b. Unacceptable wildcards: `*`, [^], [!] 
c. Escape clause is not supported yet.

Conditional Expressions in Cider
-----------------------------------
1) COALESCE
^^^^^^^^^^^^^
The COALESCE expression is a syntactic shortcut for the CASE expression

The code COALESCE(expression1,...n) is executed in Cider as the following CASE expression:

.. code-block:: sql

        CASE  
        WHEN (expression1 IS NOT NULL) THEN expression1  
        WHEN (expression2 IS NOT NULL) THEN expression2  
        ...  
        ELSE expressionN  
        END

Example: 

.. code-block:: sql

        SELECT COALESCE(col_1, col_2, 777) FROM test


is equal to

.. code-block:: sql

        SELECT CASE WHEN col_1 is not null THEN col_1 WHEN col_2 is not null THEN col_2 ELSE 777 END from test


2) IF
^^^^^^
The IF function is actually a language construct that is executed in Cider as the following CASE expression

.. code-block

:: 

        CASE
        WHEN condition THEN true_value
        [ ELSE false_value ]
        END

IF Functions: 

1. .. code-block

:: 

        if(condition, true_value)

Evaluates and returns true_value if condition is true, otherwise null is returned and true_value is not evaluated.

is equal to

.. code-block:: sql

        CASE WHEN condition THEN true_value END

2. .. code-block

:: 

        if(condition, true_value, false_value)

Evaluates and returns true_value if condition is true, otherwise evaluates and returns false_value.

is equal to

.. code-block:: sql

        CASE WHEN condition THEN true_value ELSE false_value END

SELECT DISTINCT
--------------------------------------

Mainstream databases such as Spark and Presto will transform 'SELECT DISTINCT' sql to 'GROUP BY' sql when do optimization on logical plan.

Spark: 

.. code-block:: java

        /**
        * Replaces logical [[Distinct]] operator with an [[Aggregate]] operator.
        * {{{
        *   SELECT DISTINCT f1, f2 FROM t  ==>  SELECT f1, f2 FROM t GROUP BY f1, f2
        * }}}
        */
        object ReplaceDistinctWithAggregate extends Rule[LogicalPlan] {
                def apply(plan: LogicalPlan): LogicalPlan = plan.transformWithPruning(
                        _.containsPattern(DISTINCT_LIKE), ruleId) {
                        case Distinct(child) => Aggregate(child.output, child.output, child)
                }
        }

Presto:

When execute sql `select distinct nationkey from customer`, part of the json generated by Presto is:

.. code-block:: json

        {
                "id":"2",
                "root":{
                        "@type":".AggregationNode",
                "groupingSets":{
                        "groupingKeys":[
                                {
                                "@type":"variable",
                                "sourceLocation":{
                                        "line":1,
                                        "column":17
                                },
                                "name":"nationkey",
                                "type":"bigint"
                                }
                        ],
                        "groupingSetCount":1,
                        "globalGroupingSets":[
                        ]
                }
        }

In above cases, the original 'SELECT DISTINCT' sql is converted to an Aggregation type, and the columns shoule be distinct will become 'GROUP BY' keys.

GROUP BY related function
--------------------------------------

This part will explain extended usage of GROUP BY including GROUPING SETS() , CUBE() , ROLLUP() , GROUP BY ALL/DISTINCT, and together with those combined cases.

Let's define a simple test table the schema of which is 

.. code-block:: sql

        CREATE TABLE tbl(col_a BIGINT, col_b BIGINT) 

1) GROUPING SETS
^^^^^^^^^^^^^^^^^^
Grouping sets allow users to specify multiple lists of columns to group on. The columns not part of a given sublist of grouping columns are set to **NULL**.

Example:

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY GROUPING SETS(
                (col_a, col_b),
                (col_a),
                (col_b),
                ()) 

Plan:

::

        - Output[_col0]
        - Project[projectLocality = LOCAL]
        - Aggregate(FINAL)[col_a$gid, col_b$gid, groupid][$hashvalue]
        - Aggregate(PARTIAL)[col_a$gid, col_b$gid, groupid][$hashvalue_8]
        - Project[projectLocality = LOCAL]
        - GroupId[[col_a, col_b], [col_a], [col_b], []]
        - TableScan

is **logically equivalent** to:

.. code-block:: sql

        SELECT SUM(col_a) FROM tbl GROUP BY col_a, col_b
        UNION ALL
        SELECT SUM(col_a) FROM tbl GROUP BY col_a
        UNION ALL
        SELECT SUM(col_a) FROM tbl GROUP BY col_b
        UNION ALL
        SELECT SUM(col_a) FROM tbl 

However, the only difference of them is using UNION ALL will trigger tableScan four times while only once for GROUPING SETS.

This is important not only for performance, data quality will also be a significant problem when the source table varies from time to time.

2) GROUP BY ROLLUP
^^^^^^^^^^^^^^^^^^^^
The ROLLUP operator generates all possible subtotals for a given set of columns.

Example: 

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY ROLLUP (col_a, col_b)

Plan:

::

        - Output[_col0]
        - Project[projectLocality = LOCAL]
        - Aggregate(FINAL)[col_a$gid, col_b$gid, groupid][$hashvalue]
        - Aggregate(PARTIAL)[col_a$gid, col_b$gid, groupid][$hashvalue_8]
        - Project[projectLocality = LOCAL]
        - GroupId[[], [col_a], [col_a, col_b]]
        - TableScan

is **equivalent** to:

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY GROUPING SETS(
                (col_a, col_b),
                (col_a),
                ()) 

3) GROUP BY CUBE
^^^^^^^^^^^^^^^^^^^^
The CUBE operator generates all possible grouping sets (i.e. a power set) for a given set of columns.

Example: 


.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY CUBE (col_a, col_b)

Plan:

::

        - Output[_col0]
        - Project[projectLocality = LOCAL]
        - Aggregate(FINAL)[col_a$gid, col_b$gid, groupid][$hashvalue]
        - Aggregate(PARTIAL)[col_a$gid, col_b$gid, groupid][$hashvalue_8]
        - Project[projectLocality = LOCAL]
        - GroupId[[], [col_a], [col_b], [col_a, col_b]]
        - TableScan

is **equivalent** to:

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY GROUPING SETS(
                (col_a, col_b),
                (col_a),
                (col_b),
                ()); 

4) GROUP BY ALL/DISTINCT
^^^^^^^^^^^^^^^^^^^^^^^^^^^

We don't need to handle ALL/DISTINCT in Cider, since it will be transfered to GROUPING SETS when generating Presto plans.
The ALL and DISTINCT quantifiers determine whether duplicate grouping sets each produce distinct output rows.
This is particularly useful when multiple complex grouping sets are combined in the same query.

Example1: 

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY ALL ROLLUP (col_a, col_b), CUBE (col_a, col_b)

Plan:

::

        - Output[_col0]
        - Project[projectLocality = LOCAL]
        - Aggregate(FINAL)[col_a$gid, col_b$gid, groupid][$hashvalue]
        - Aggregate(PARTIAL)[col_a$gid, col_b$gid, groupid][$hashvalue_8]
        - Project[projectLocality = LOCAL]
        - GroupId[[], [col_a], [col_a, col_b], [col_a], [col_a], [col_a, col_b], [col_b], [col_b, col_a], [col_b, col_a], [col_a, col_b], [col_a, col_b], [col_a, col_b]]
        - TableScan

is **equivalent** to:

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY
        GROUPING SETS ((col_a, col_b), (col_a), ()),
        GROUPING SETS((col_a, col_b), (col_a), (col_b), ()); 

Example2: 

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY DISTINCT ROLLUP (col_a, col_b), CUBE (col_a, col_b)

Plan:

::

        - Output[_col0]
        - Project[projectLocality = LOCAL]
        - Aggregate(FINAL)[col_a$gid, col_b$gid, groupid][$hashvalue]
        - Aggregate(PARTIAL)[col_a$gid, col_b$gid, groupid][$hashvalue_8]
        - Project[projectLocality = LOCAL]
        - GroupId[[], [col_a], [col_a, col_b], [col_b]]
        - TableScan

is **equivalent** to:

.. code-block:: sql

        SELECT SUM(col_a)
        FROM tbl
        GROUP BY
        GROUPING SETS ((col_a, col_b), (col_a), (col_b), ());

Using ALL will leave all duplicate grouping sets while DISTINCT will dedup them.

5) GROUPING() operation
^^^^^^^^^^^^^^^^^^^^^^^^^^

We can find the usage of SELECT GROUPING(col_a, col_b ...) FROM table GROUP BY ROLLUP (col_a, col_b ...)  in TPC-DS Query27.
The grouping operation returns a bit set converted to decimal, indicating which columns are present in a grouping.
It must be used in conjunction with GROUPING SETS, ROLLUP, CUBE or GROUP BY and its arguments must match exactly the columns referenced in the corresponding GROUPING SETS, ROLLUP, CUBE or GROUP BY clause.

Example: 

.. code-block:: sql

        SELECT SUM(col_a), col_a, col_b, GROUPING(col_a, col_b)
        FROM tbl
        GROUP BY GROUPING SETS((col_a), (col_b));

Result:

::

        _col0 | col_a | col_b | _col3
        -------+-------+-------+-------
        2 | NULL  |     3 |     2
        4 |     2 | NULL  |     1
        3 |     3 | NULL  |     1
        1 | NULL  |     1 |     2
        1 |     1 | NULL  |     1
        2 | NULL  |     2 |     2
        3 | NULL  |     4 |     2
        (7 rows)

The example shows when GROUP BY col_a, the bit set should be 01, so the value of _col3 is 1.
When GROUP BY col_b, the bit set should be 10, thus the value of _col3 is 2.

The GROUPING(col_a, col_b) results in _col3 and it represents a bit set converted to BIGINT.
Each column in GROUPING  operation will take one bit and it will be set to 0 if the corresponding column is included in the grouping and to 1 otherwise.

8 ALL/ANY
---------

In SQL, 'ALL' and 'ANY' are used to decorate compare operators(<, <=, =, !=, >, >=) between column values and a subquery result.

'ALL' will return TRUE if the value matches **all** corresponding values in the subquery, while 'ANY' returns TRUE if it matches **any** single one.

Example:
^^^^^^^^^

Given test.col_i8 is
::

         col_i8 
        --------
        5 
        3 
        3 
        (3 rows)


then execute the following sql::

        SELECT col_i8 < ALL(VALUES 4,5,6) from test;


will return::

         _col0 
        -------
        false 
        true  
        true  
        (3 rows)

because 5 is not less than 4 while 3 is less than all the right values.

For above case, the logical plan generated by **Presto** is:

1. use the aggregate function **MIN** to get the min value of right rows.

2. use a **cross join** to generate the boolean results, whose left and right arguments are left rows and the min value in the first step.

The logical plan tree::

        - Output[_col0] => [expr_3:boolean]                                                                                                                                  >
                _col0 := expr_3 (1:23)                                                                                                                                       >
            - RemoteStreamingExchange[GATHER] => [expr_3:boolean]                                                                                                            >
                - Project[projectLocality = LOCAL] => [expr_3:boolean]                                                                                                       >
                        expr_3 := SWITCH(count_all, WHEN(BIGINT'0', BOOLEAN'true'), ((col_i8_0) < (min)) AND (SWITCH(BOOLEAN'true', WHEN((count_all) <> (count_non_null), nul>
                    - CrossJoin => [col_i8_0:integer, min:integer, count_all:bigint, count_non_null:bigint]                                                                  >
                                Distribution: REPLICATED                                                                                                                     >
                        - ScanProject[table = TableHandle {connectorId='hive', connectorHandle='HiveTableHandle{schemaName=tpch, tableName=test, analyzePartitionValues=Optio>
                                col_i8_0 := CAST(col_i8 AS integer) (1:49)                                                                                                   >
                                LAYOUT: tpch.test{}                                                                                                                          >
                                col_i8 := col_i8:tinyint:0:REGULAR (1:48)                                                                                                    >
                        - LocalExchange[SINGLE] () => [min:integer, count_all:bigint, count_non_null:bigint]                                                                 >
                            - RemoteStreamingExchange[REPLICATE] => [min:integer, count_all:bigint, count_non_null:bigint]                                                   >
                                - Aggregate => [min:integer, count_all:bigint, count_non_null:bigint]                                                                        >
                                        min := "presto.default.min"((field)) (1:17)                                                                                          >
                                        count_all := "presto.default.count"(*) (1:17)                                                                                        >
                                        count_non_null := "presto.default.count"((field)) (1:17)                                                                             >
                                    - Values => [field:integer]                                                                                                              >
                                            (INTEGER'4')                                                                                                                     >
                                            (INTEGER'5')                                                                                                                     >
                                            (INTEGER'6')          

For **<, <=, >, >=**
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The only two changes in plan are:

1. the aggregate function to get the max or min value.

2. the compare operator in the project step.

+------------+------------+-----------+
| ALL/ANY    | operator   | function  |
+============+============+===========+
| ALL        | </<=       | MIN       |
+------------+------------+-----------+
| ALL        | >/>=       | MAX       |
+------------+------------+-----------+
| ANY        | </<=       | MAX       |
+------------+------------+-----------+
| ANY        | >/>=       | MIN       |
+------------+------------+-----------+
  

For **=, !=**
^^^^^^^^^^^^^

For '**=**' in 'ALL' cases, there will be two aggregate functions **MIN** and **MAX**, and the project expression will become **(min) = (max)) AND ((expr) = (max))**. 

While for '**!=**', there will be only a semi join between left rows(expr) and right rows to get boolean results, then a **NOT** operation will be implemented to get final results.

For 'ANY' cases, the plans for '**=**' and '**!=**' are exactly the same as those of '**!=**' and '**=**' in 'ALL' cases.