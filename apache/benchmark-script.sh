#!/bin/bash
for i in {1..20}
do
    echo starting apache with $i core\(s\)
    sudo taskset -c 1-$i /opt/smokewagon/httpd/bin/httpd -k start -f /home/milkv/sophgo/smokewagon-benchmarks/apache/httpd.conf

    echo hey
    taskset -c 57-63 /home/milkv/go/bin/hey -disable-keepalive -z 10s http://localhost/ > /home/milkv/sophgo/smokewagon-benchmarks/apache/results/hey-output-inactive-$(printf '%02d' "$i").txt

    echo shutting down apache
    sudo /opt/smokewagon/httpd/bin/httpd -k graceful-stop -f /home/milkv/sophgo/smokewagon-benchmarks/apache/httpd.conf
    
    echo waiting for process and port to be freed
    while pgrep -x httpd >/dev/null; do
        sleep 0.5
    done

    while ss -ltnp | grep -q 80; do
        sleep 0.5
    done
done

