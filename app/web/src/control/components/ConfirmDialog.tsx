import { Button, Input, Modal } from 'antd';
import { useEffect, useState } from 'react';

export function ConfirmDialog({
  open,
  title,
  description,
  intentLabel,
  confirmText = 'Submit intent',
  onCancel,
  onConfirm,
}: {
  open: boolean;
  title: string;
  description: string;
  intentLabel: string;
  confirmText?: string;
  onCancel: () => void;
  onConfirm: (reason: string) => Promise<void> | void;
}) {
  const [reason, setReason] = useState('');
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    if (!open) setReason('');
  }, [open]);

  async function handleConfirm() {
    setSubmitting(true);
    try {
      await onConfirm(reason || intentLabel);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <Modal
      className="control-modal"
      open={open}
      title={title}
      footer={[
        <Button key="cancel" onClick={onCancel}>
          Cancel
        </Button>,
        <Button key="confirm" type="primary" loading={submitting} onClick={handleConfirm}>
          {confirmText}
        </Button>,
      ]}
      onCancel={onCancel}
      destroyOnHidden
    >
      <p className="control-dialog-description">{description}</p>
      <label className="control-field-label" htmlFor="control-intent-reason">
        Reason
      </label>
      <Input.TextArea
        id="control-intent-reason"
        value={reason}
        rows={3}
        placeholder={intentLabel}
        onChange={(event) => setReason(event.target.value)}
      />
      <div className="control-dialog-note">This creates an AdminService intent record. It does not execute a local process action.</div>
    </Modal>
  );
}
