// Package audit provides a thin wrapper that records analyst actions
// to the audit_log table without leaking storage details to callers.
package audit

import (
	"context"
	"encoding/json"
	"log"

	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

type Recorder struct {
	Store *store.Store
}

// Record persists an audit entry. Failures are logged but do not propagate
// (audit must never block the user-visible operation).
func (r *Recorder) Record(ctx context.Context, actor, role, action, targetType string, targetID int64, diff any) {
	var raw json.RawMessage
	if diff != nil {
		if b, err := json.Marshal(diff); err == nil {
			raw = b
		}
	}
	rolePtr := &role
	if role == "" {
		rolePtr = nil
	}
	if err := r.Store.AppendAudit(ctx, &store.AuditEntry{
		Actor:      actor,
		ActorRole:  rolePtr,
		Action:     action,
		TargetType: targetType,
		TargetID:   targetID,
		Diff:       raw,
	}); err != nil {
		log.Printf("[audit] persist failed: %v", err)
	}
}
