// Package rbac implements a minimal token-based RBAC for the SOC API.
//
// Tokens are pre-shared via env (Phase-6 simplicity); production should
// front this with K8s TokenReview or OIDC. Each token maps to a role:
//   viewer  : read-only
//   editor  : ack/resolve incidents
//   admin   : approve/reject response actions
package rbac

import (
	"context"
	"net/http"
	"strings"
)

type Role string

const (
	RoleViewer Role = "viewer"
	RoleEditor Role = "editor"
	RoleAdmin  Role = "admin"
)

type ctxKey int

const (
	ctxKeyUser ctxKey = iota
	ctxKeyRole
)

type TokenMap map[string]struct {
	User string
	Role Role
}

// ParseTokens parses an env value of the form:
//   "alice:viewer:tok123,bob:editor:tok456,root:admin:tokABC"
// Empty input → empty map (RBAC disabled; all requests anonymous viewer).
func ParseTokens(env string) TokenMap {
	out := TokenMap{}
	for _, entry := range strings.Split(env, ",") {
		entry = strings.TrimSpace(entry)
		if entry == "" {
			continue
		}
		parts := strings.Split(entry, ":")
		if len(parts) != 3 {
			continue
		}
		user, role, tok := parts[0], Role(parts[1]), parts[2]
		out[tok] = struct {
			User string
			Role Role
		}{User: user, Role: role}
	}
	return out
}

// Middleware authenticates the request using a Bearer token. If RBAC is
// disabled (empty map) it injects an anonymous viewer.
func Middleware(tokens TokenMap) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			user, role := "anonymous", RoleViewer
			if len(tokens) > 0 {
				h := r.Header.Get("Authorization")
				tok := strings.TrimPrefix(h, "Bearer ")
				if tok == h || tok == "" {
					http.Error(w, "missing bearer token", http.StatusUnauthorized)
					return
				}
				v, ok := tokens[tok]
				if !ok {
					http.Error(w, "invalid token", http.StatusUnauthorized)
					return
				}
				user, role = v.User, v.Role
			}
			ctx := context.WithValue(r.Context(), ctxKeyUser, user)
			ctx = context.WithValue(ctx, ctxKeyRole, role)
			next.ServeHTTP(w, r.WithContext(ctx))
		})
	}
}

func User(ctx context.Context) string {
	v, _ := ctx.Value(ctxKeyUser).(string)
	if v == "" {
		return "anonymous"
	}
	return v
}

func RoleOf(ctx context.Context) Role {
	v, _ := ctx.Value(ctxKeyRole).(Role)
	if v == "" {
		return RoleViewer
	}
	return v
}

// RequireRole gates a handler chain. Permission ladder: admin > editor > viewer.
func RequireRole(min Role) func(http.Handler) http.Handler {
	rank := map[Role]int{RoleViewer: 1, RoleEditor: 2, RoleAdmin: 3}
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if rank[RoleOf(r.Context())] < rank[min] {
				http.Error(w, "forbidden: requires "+string(min), http.StatusForbidden)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}
