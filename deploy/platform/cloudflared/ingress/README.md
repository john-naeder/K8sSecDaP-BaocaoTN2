# Traefik IngressRoute manifests

Traefik IngressRoute / Middleware CRs per exposed app live here.
ArgoCD `ingress-routing` Application syncs this folder into namespace `traefik`.

Tekton pipeline `ingress-sync` writes ConfigMaps here that Traefik picks up.
