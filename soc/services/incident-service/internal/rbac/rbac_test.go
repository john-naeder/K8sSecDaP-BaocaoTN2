package rbac

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestParseTokens(t *testing.T) {
	m := ParseTokens("alice:viewer:tk1, bob:editor:tk2 ,root:admin:tk3")
	if len(m) != 3 {
		t.Fatalf("want 3 tokens, got %d", len(m))
	}
	if m["tk2"].User != "bob" || m["tk2"].Role != RoleEditor {
		t.Errorf("bob entry wrong: %+v", m["tk2"])
	}
	if len(ParseTokens("")) != 0 {
		t.Errorf("empty input should yield empty map")
	}
}

func TestMiddleware_RequiresToken(t *testing.T) {
	tokens := ParseTokens("a:viewer:tok")
	h := Middleware(tokens)(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(200)
	}))
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest("GET", "/", nil))
	if rr.Code != 401 {
		t.Errorf("expected 401, got %d", rr.Code)
	}
}

func TestMiddleware_AllowsValidToken(t *testing.T) {
	tokens := ParseTokens("a:editor:tok")
	h := Middleware(tokens)(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if User(r.Context()) != "a" || RoleOf(r.Context()) != RoleEditor {
			t.Errorf("ctx wrong: user=%s role=%s", User(r.Context()), RoleOf(r.Context()))
		}
		w.WriteHeader(200)
	}))
	rr := httptest.NewRecorder()
	req := httptest.NewRequest("GET", "/", nil)
	req.Header.Set("Authorization", "Bearer tok")
	h.ServeHTTP(rr, req)
	if rr.Code != 200 {
		t.Errorf("expected 200, got %d", rr.Code)
	}
}

func TestMiddleware_AnonymousWhenNoTokenMap(t *testing.T) {
	h := Middleware(ParseTokens(""))(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if User(r.Context()) != "anonymous" || RoleOf(r.Context()) != RoleViewer {
			t.Errorf("expected anon viewer, got user=%s role=%s",
				User(r.Context()), RoleOf(r.Context()))
		}
		w.WriteHeader(200)
	}))
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, httptest.NewRequest("GET", "/", nil))
	if rr.Code != 200 {
		t.Errorf("got %d", rr.Code)
	}
}

func TestRequireRole(t *testing.T) {
	cases := []struct {
		name       string
		token      string
		minRole    Role
		wantStatus int
	}{
		{"viewer-needs-editor", "a:viewer:t1", RoleEditor, 403},
		{"editor-needs-editor", "b:editor:t2", RoleEditor, 200},
		{"admin-needs-editor",  "c:admin:t3",  RoleEditor, 200},
		{"editor-needs-admin",  "d:editor:t4", RoleAdmin,  403},
		{"admin-needs-admin",   "e:admin:t5",  RoleAdmin,  200},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			tokens := ParseTokens(tc.token)
			tok := tc.token[len(tc.token)-2:] // last "tN"
			h := Middleware(tokens)(RequireRole(tc.minRole)(http.HandlerFunc(
				func(w http.ResponseWriter, _ *http.Request) { w.WriteHeader(200) },
			)))
			req := httptest.NewRequest("GET", "/", nil)
			req.Header.Set("Authorization", "Bearer "+tok)
			rr := httptest.NewRecorder()
			h.ServeHTTP(rr, req)
			if rr.Code != tc.wantStatus {
				t.Errorf("got %d want %d", rr.Code, tc.wantStatus)
			}
		})
	}
}
