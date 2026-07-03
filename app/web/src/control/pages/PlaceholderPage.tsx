export default function PlaceholderPage({
  title,
  description,
}: {
  title: string;
  description: string;
}) {
  return (
    <div className="control-placeholder-page">
      <div className="control-page-heading">
        <div>
          <h1>{title}</h1>
          <p>{description}</p>
        </div>
      </div>
      <section className="control-panel control-placeholder-panel">
        <h2>待接入</h2>
        <p>该模块的 控制面数据模型与控制面接口将在后续迭代接入。</p>
      </section>
    </div>
  );
}
