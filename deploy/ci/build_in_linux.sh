#!/bin/bash


# # c_compiler=$1

# # cpp_compiler=$2

# # 安装 Ninja（如果尚未安装）
# if ! command -v ninja &> /dev/null; then
#     echo "Ninja is not installed. Installing Ninja..."
#     # sudo apt-get update
#     sudo apt-get install -y ninja-build
# fi

# #构建目录
# mkdir build
# #
# cd build
# #执行CMAKE
# cmake -G Ninja ..
# #构建
# ninja


#构建主程序
mkdir build
#
cd build
#执行CMAKE
cmake ..
#构建
make -j$(nproc)
#切换回主目录
cd ..


#构建依赖环境
cd env/redis-7.2.1 && make MALLOC=libc -j$(nproc)
cd ../..
mkdir bin/env -p
mkdir bin/env/redis -p
cp env/redis-7.2.1/src/redis-server bin/env/redis/redis-server
cp .cmake/redis_inux.conf.in    bin/env/redis/redis.config

#复制部署脚本
mkdir bin/env/scripts -p
cp -r env/scripts/ bin/env
