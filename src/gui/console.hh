// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; eval: (progn (c-set-style "stroustrup") (c-set-offset 'innamespace 0) (c-set-offset 'inextern-lang 0)); -*-
// vi:set ts=4 sts=4 sw=4 noet :
// Copyright 2014 The pyRASMUS development team
// 
// This file is part of pyRASMUS.
// 
// pyRASMUS is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
// 
// pyRASMUS is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with pyRASMUS.  If not, see <http://www.gnu.org/licenses/>
#ifndef __SRC_GUI_CONSOLE_H__
#define __SRC_GUI_CONSOLE_H__

#include <QPlainTextEdit>

class Settings;
class ConsolePrivate;

class Console: public QPlainTextEdit {
	Q_OBJECT
public:
	Console(QWidget * parent);
	~Console();
	void keyPressEvent(QKeyEvent *e);

public slots:
	void incomplete();
	void complete();
	void display(QString msg);
	void visualUpdate(Settings *);
	void doCancel();
	void bussy(bool);
	void gotoEnd();
	void doPrintConsole();
	void insertFromMimeData(const QMimeData *) override;
signals:
	void run(QString line);
	void cancel();
	void quit();

private:
	void runLine(const QString &tmp);
	void pasteContinue();

	QStringList pasteBuffer;
	bool pasteActive;
	int pasteIndex;

	ConsolePrivate * d;
};


#endif //__SRC_GUI_CONSOLE_H__
