#!/bin/bash

# 默认值
CONCURRENCY=10
TOTAL_REQUESTS=100
URL=""
OUTPUT_FILE="/tmp/wget_bench_times.txt"
REQUEST_DELAY=0

# 清空临时文件
> "$OUTPUT_FILE"

usage() {
    echo "Usage: $0 -c <concurrency> -n <total_requests> <url>"
    echo "Example: $0 -c 5 -n 100 http://192.168.1.1/index.html"
    exit 1
}

# 解析命令行参数
while getopts ":c:n:" opt; do
    case ${opt} in
        c)
            CONCURRENCY=$OPTARG
            ;;
        n)
            TOTAL_REQUESTS=$OPTARG
            ;;
        *)
            usage
            ;;
    esac
done
shift $((OPTIND -1))

# 获取 URL
URL="$1"
if [ -z "$URL" ]; then
    usage
fi

echo "压测地址: $URL"
echo "并发数: $CONCURRENCY 请求/秒"
echo "总请求数: $TOTAL_REQUESTS"

# 每个线程要发送的请求数
REQUESTS_PER_THREAD=$(( (TOTAL_REQUESTS + CONCURRENCY - 1) / CONCURRENCY ))

# 每个线程函数
run_thread() {
    for i in $(seq 1 $REQUESTS_PER_THREAD); do
        # 使用 wget 发送请求并记录响应时间
        START=$(date +%s%3N)
        wget -q -O /dev/null "$URL"
        END=$(date +%s%3N)
        ELAPSED=$(echo "scale=3; ($END - $START)/1000" | bc)

        echo "$ELAPSED" >> "$OUTPUT_FILE"
        sleep "$REQUEST_DELAY"
    done
}

# 开始计时整体耗时
START_TIME=$(date +%s%3N)

# 启动并发线程
for i in $(seq 1 $CONCURRENCY); do
    run_thread &
done

# 等待所有任务完成
wait

# 结束计时
END_TIME=$(date +%s%3N)
TOTAL_TIME_MS=$(echo "scale=3; ($END_TIME - $START_TIME)/1000" | bc)

# 统计结果
count=$(wc -l < "$OUTPUT_FILE")
avg_time=$(awk '{sum += $1} END {print sum/NR}' "$OUTPUT_FILE")
min_time=$(sort -n "$OUTPUT_FILE" | head -n1)
max_time=$(sort -n "$OUTPUT_FILE" | tail -n1)

# 输出结果
echo "-------------------------------"
echo "总请求数:      $count"
echo "总耗时（秒）: $TOTAL_TIME_MS"
echo "平均响应时间: $avg_time 秒"
echo "最快响应时间: $min_time 秒"
echo "最慢响应时间: $max_time 秒"
echo "QPS（近似）:  $(echo "scale=2; $count / $TOTAL_TIME_MS" | bc)"
echo "-------------------------------"

# 清理临时文件
rm -f "$OUTPUT_FILE"
