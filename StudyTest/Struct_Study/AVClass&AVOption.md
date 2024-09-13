# 一. AVClass
### 1.1 . 结构体定义定义：
用于描述FFmpeg中各种上下文结构(如AVCodecContext、AVFormatContext等)的类信息。它的主要作用是提供一种统一的方式来处理和管理这些不同类型的上下文结构；

### 1.2 . 结构体变量含义：
* `class_name`：class的名称,通常与关联的上下文结构类型名称相同;
* `item_name`:函数指针,用于返回与该类关联的特定上下文实例的名称；
* `version`: 创建此结构时的LIBAVUTIL_VERSION，用于向后兼容性
* `option`：AVOption类型的数组，在定义初始化时指向一个**静态数组**；
* `child_class_iterate`：函数指针,用于迭代所有可能的启用了AVOptions的子对象的AVClass；
* `child_next`：函数指针,用于返回下一个启用了AVOptions的子对象；
* `category`: 用于可视化的类别(如颜色),快速识别一个 AVClass 实例·  属于哪种类型的组件（如输入、输出、编码器、解码器等）,也可以使得日志根据组件过滤；
* `get_category`: 自定义一个函数返回AVClassCategory类型；

# 二. AVOption：
### 2.1 . 结构体定义：
* AVOption用于在FFmpeg中描述结构体中的成员变量。它最主要的作用可以概括为两个字：“赋值”。一个AVOption结构体包含了变量名称，简短的帮助，取值等等信息；AVOption可以使用字符串为任何类型的变量赋值
* 所有和AVOption有关的数据都存储在AVClass结构体中。如果一个结构体（例如AVFormatContext或者AVCodecContext）想要支持AVOption的话，它的第一个成员变量必须是一个指向AVClass结构体的指针。该AVClass中的成员变量option必须指向一个AVOption类型的静态数组；
* AVClass最主要的作用就是给结构体（例如AVFormatContext等）增加AVOption功能的支持。换句话说AVClass就是AVOption和目标结构体之间的“桥梁”。AVClass要求必须声明为目标结构体的第一个变量；
### 2.2 . 结构体定义：
* `name`：外显变量名称，用于标识；
* `help`：英文描述，用于标识改选项；
* `offset`：选项值在相关结构体中的偏移量。对于命名常量,应该设为0；
* `type`：对应变量的类型，取值为一个枚举类型AVOptionType；
* `default_val`：根据变量类型存储一个不同类型的默认值；
* `min`：选项的最小有效值；
* `max`：选项的最大有效值；
* `flags`：选项的标志,是AV_OPT_FLAG_常量的组合；
* `unit`：选项所属的逻辑单元。非常量选项和对应的命名常量共享相同的单元。可以为NULL（也就是对应变量的常量取值）；

# 三. 链接：
* [结构体成员管理系统-AVClass](https://blog.csdn.net/yangguoyu8023/article/details/107745062)
