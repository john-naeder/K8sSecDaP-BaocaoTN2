POD=$(kubectl -n zt-mapper get pod -l app.kubernetes.io/name =zt-pipeline-ebpf \
 -o jsonpath='{.items[0].metadata.name}')