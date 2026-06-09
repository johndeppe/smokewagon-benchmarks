#!/bin/bash
for i in {1..56}
do
    echo testing apache with $i core\(s\)

    sudo taskset -c 1-$i /opt/smokewagon/httpd-smokewagon/bin/httpd -k start -f /home/milkv/sophgo/smokewagon-benchmarks/apache/httpd-smokewagon.conf

    echo hey
    taskset -c 57-63 /home/milkv/go/bin/hey -disable-keepalive -z 10s http://localhost/ > /home/milkv/sophgo/smokewagon-benchmarks/apache/results/hey-output-smokewagon-$(printf '%02d' "$i").txt

    echo stopping httpd
    sudo /opt/smokewagon/httpd-smokewagon/bin/httpd -k graceful-stop -f /home/milkv/sophgo/smokewagon-benchmarks/apache/httpd-smokewagon.conf
    
    echo waiting for process to die
    while pgrep -x httpd >/dev/null; do
        sleep 0.5
    done

    echo waiting for port to be freed
    while ss -ltnp | grep -q 80; do
        sleep 0.5
    done
done

