// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Tools > Java Doctor: which Java every instance will use, what is wrong with it and how to fix it. The prediction lives in
// zero/java/JavaDoctor (pure); this view gathers the inputs, shows the result and, after an explicit confirmation, applies a suggested Java
// to one instance. It never changes a Java setting on its own.
#pragma once

#include <QList>
#include <QWidget>
#include "tasks/Task.h"
#include "zero/java/JavaDoctor.h"

class QPushButton;
class QScrollArea;
class QStackedLayout;
class QVBoxLayout;

namespace Zero {

class JavaDoctorView : public QWidget {
    Q_OBJECT
   public:
    explicit JavaDoctorView(QWidget* parent = nullptr);

    /// One-line description shown as the page subtitle while the tool is open.
    static QString description();

    /// Top bar widget (the Rescan button); owned by the view.
    QWidget* controlsWidget() const { return m_controls; }
    /// Called whenever the tool is opened: looks for Java runtimes again unless that is already running.
    void open();
    bool isLoading() const { return m_loading; }
    bool hasEvaluated() const { return m_evaluated; }

   signals:
    void showInstanceRequested(const QString& instanceId);
    /// "Java settings": open the launcher settings on the Java page.
    void javaSettingsRequested();
    /// The first / a new evaluation is on screen.
    void evaluationFinished();

   private:
    struct Row {
        Java::InstanceJava java;
        Java::Diagnosis diagnosis;
    };

    void scan();
    void finishScan();
    void evaluate();
    void rebuild();
    void updateBusy();
    void askUseJava(const QString& instanceId, const Java::Runtime& runtime);

    QWidget* m_controls;
    QPushButton* m_rescan;
    QStackedLayout* m_stack;
    QScrollArea* m_scroll;
    QWidget* m_host = nullptr;

    QList<Java::Runtime> m_runtimes;
    Java::GlobalJava m_global;
    QList<Row> m_rows;
    bool m_loading = false;
    bool m_evaluated = false;
    Task::Ptr m_task;
};

}  // namespace Zero
