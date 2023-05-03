## 2023-05-3 五一结束 实现了 map + array + stable_sort
sort一开始想简单了。实际实现时发现要考虑异常和gc。就不能用锁定数组，然后原地排序实现了。
不得已复制一份数据，排序。然后想着都这样了，就实现个稳定排序吧。偷懒使用了 qsort_s, 兼容性也许会有问题。

编码过程一言难尽，写到晚上4点半，还在调试。
虚脱的五一？充实的五一？
目前来看，大多数想要的修改都已经改完了。后续看看 vscode lsp 的支持了。

PS1: stable_sort 耗时时 lua sort 的 一半。lua sort 比想象的快呀。
PS2: lua 的 array + map 实现的table 如果使用恰当。性能会非常不错。【不过 for pairs 是个坑。可以单独修改下实现 】
PS3: c# linq 的 OrderBy 使用延迟复制排序的方式。他的 KeyValuePair 时单独的结构，lua就不行了。

## 2023-05-02 五一期间实现 map + array
念头一瞬间。
思考5分钟。
编码1天。
调试2天。

时间比例大致如是。

实现后，发现日常 get set 性能比lua差。
但没什么办法。map 多了一层内存索引。
如果不要这层 hash 索引，那么 lua 当前的 table 实现就是比较好的选择了。
也有好的地方嘛：遍历有序，forin 很快。还可以实现高效的 sort。