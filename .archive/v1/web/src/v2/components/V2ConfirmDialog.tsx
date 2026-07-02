import { Button, Input, Modal } from 'antd';
import { useEffect, useState } from 'react';

export function V2ConfirmDialog({
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
      className="v2-modal"
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
      destroyOnClose
    >
      <p className="v2-dialog-description">{description}</p>
      <label className="v2-field-label" htmlFor="v2-intent-reason">
        Reason
      </label>
      <Input.TextArea
        id="v2-intent-reason"
        value={reason}
        rows={3}
        placeholder={intentLabel}
        onChange={(event) => setReason(event.target.value)}
      />
      <div className="v2-dialog-note">This creates an AdminService intent record. It does not execute a local process action.</div>
    </Modal>
  );
}
