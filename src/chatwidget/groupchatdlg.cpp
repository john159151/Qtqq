#include "groupchatdlg.h"
#include "ui_groupchatdlg.h"

#include <QDateTime>
#include <QList>
#include <QTcpSocket>
#include <QDebug>

#include "json/json.h"

#include "chatwidget/chatdlg_manager.h"
#include "core/captchainfo.h"
#include "core/groupchatlog.h"
#include "core/groupimgloader.h"
#include "core/sockethelper.h"
#include "protocol/qq_protocol.h"
#include "roster/group_presister.h"
#include "rostermodel/roster_model.h"
#include "roster/roster.h"
#include "skinengine/qqskinengine.h"

GroupChatDlg::GroupChatDlg(Group *group, ChatDlgType type, QWidget *parent) :
    QQChatDlg(group, type, parent),
    ui(new Ui::GroupChatDlg()),
    model_ (new RosterModel)
{
    ui->setupUi(this);

    initUi();  
    updateSkin();
    initConnections();

    te_input_.setFocus();
    ui->lv_members_->setModel(model_);

    setupMemberList();
}

void GroupChatDlg::setupMemberList()
{
    Group *group = static_cast<Group *>(talkable_);

    if ( group->memberCount() == 0 )
    {
        Protocol::QQProtocol *protocol = Protocol::QQProtocol::instance();
        protocol->requestGroupMemberList((Group *)talkable_);
    }
    else
    {
        QList<Contact *> members = ((Group *)talkable_)->members();
        foreach ( Contact *contact, members )
        {
            model_->addContactItem(contact);
            replaceUnconverId(contact);
        }
    }
}

GroupChatDlg::~GroupChatDlg()
{
    if ( model_ )
    {
        delete model_;
        model_ = NULL;
    }

    disconnect();
    delete ui;
}

void GroupChatDlg::initUi()
{
    setWindowTitle(talkable_->name());
    ui->lbl_name_->setText(talkable_->name());
    ui->announcement->setPlainText(((Group *)talkable_)->announcement());

    model_->setIconSize(QSize(25, 25));

    QPixmap pix = talkable_->avatar();
    if ( !pix.isNull() )
        ui->lbl_avatar_->setPixmap(pix);
    else
        ui->lbl_avatar_->setPixmap(QPixmap(QQSkinEngine::instance()->skinRes("default_group_avatar")));

    ui->btn_send_key->setMenu(send_type_menu_);

    ui->splitter_left_->insertWidget(0, &msgbrowse_);
    ui->splitter_left_->setChildrenCollapsible(false);
    ui->v_layout_left_->insertWidget(1, &te_input_);
    ui->splitter_main->setChildrenCollapsible(false);
    ui->splitter_right->setChildrenCollapsible(false);

    //设置分割器大小
    QList<int> main_sizes;
    main_sizes.append(500);
    main_sizes.append(ui->splitter_right->midLineWidth());
    ui->splitter_main->setSizes(main_sizes);

    QList<int> left_sizes;
    left_sizes.append(500);
    left_sizes.append(ui->splitter_left_->midLineWidth());
    ui->splitter_left_->setSizes(left_sizes);

    QList<int> right_sizes;
    right_sizes.append(200);
    right_sizes.append(this->height());
    ui->splitter_right->setSizes(right_sizes);

    this->resize(600, 500);
}

void GroupChatDlg::initConnections()
{
    connect(talkable_, SIGNAL(dataChanged(QVariant, TalkableDataRole)), this, SLOT(onTalkableDataChanged(QVariant, TalkableDataRole)));

    Group *group = static_cast<Group *>(talkable_);
    connect(group, SIGNAL(memberDataChanged(Contact *, TalkableDataRole)), this, SLOT(onGroupMemberDataChanged(Contact *, TalkableDataRole)));
    connect(group, SIGNAL(memberAdded(Contact *)), this, SLOT(onMemberAdded(Contact *)));
    connect(group, SIGNAL(memberRemoved(Contact *)), this, SLOT(onMemberRemoved(Contact *)));

    connect(ui->btn_send_img, SIGNAL(clicked(bool)), this, SLOT(openPathDialog(bool)));
    connect(ui->btn_send_msg, SIGNAL(clicked()), this, SLOT(sendMsg())); connect(ui->btn_qqface, SIGNAL(clicked()), this, SLOT(openQQFacePanel()));
    connect(ui->btn_close, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->btn_chat_log, SIGNAL(clicked()), this, SLOT(openChatLogWin()));
    connect(ui->lv_members_, SIGNAL(doubleClicked(const QModelIndex &)), model_, SLOT(onDoubleClicked(const QModelIndex &)));

    connect(&msgbrowse_, SIGNAL(senderLinkClicked(QString)), this, SLOT(openSessOrFriendChatDlg(QString)));
}

void GroupChatDlg::updateSkin()
{

}

void GroupChatDlg::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    QQChatDlg::closeEvent(event);

    GroupPresister::instance()->setActivateFlag(talkable_->id());
}

void GroupChatDlg::openChatDlgByDoubleClicked(const QModelIndex &index)
{
    /*
    QString member_id =  static_cast<RosterIndex *>(index.internalPointer());
    openSessOrFriendChatDlg(contact->id());
    */
}

void GroupChatDlg::openSessOrFriendChatDlg(QString id)
{
    Roster *roster = Roster::instance();
    Contact *contact = roster->contact(id);
    if ( contact )
        ChatDlgManager::instance()->openFriendChatDlg(id);
    else
    {
        msg_sig_ = getMsgSig(talkable_->id(), id);
        ChatDlgManager::instance()->openSessChatDlg(id, talkable_->id());
    }
}

QString GroupChatDlg::getMsgSig(QString gid,  QString to_id)
{
    QString msg_sig_url = "/channel/get_c2cmsg_sig2?id="+ gid +"&to_uin=" + to_id +
        "&service_type=0&clientid=5412354841&psessionid=" + CaptchaInfo::instance()->psessionid() +"&t=" + QString::number(QDateTime::currentMSecsSinceEpoch());

    Request req;
    req.create(kGet, msg_sig_url);
    req.addHeaderItem("Host", "d.web2.qq.com");
    req.addHeaderItem("Content-Type", "utf-8");
    req.addHeaderItem("Referer", "http://d.web2.qq.com/proxy.html?v=20110331002");
    req.addHeaderItem("Cookie", CaptchaInfo::instance()->cookie());

    QTcpSocket fd;
    fd.connectToHost("d.web2.qq.com", 80);
    fd.write(req.toByteArray());

    QByteArray result;
    socketReceive(&fd, result);
    fd.close();

    int sig_s_idx = result.indexOf("value")+8;
    int sig_e_idx = result.indexOf('"', sig_s_idx);
    QString sig = result.mid(sig_s_idx, sig_e_idx - sig_s_idx);

    return sig;
}

ImgLoader *GroupChatDlg::getImgLoader() const
{
    return new GroupImgLoader();
}

QQChatLog *GroupChatDlg::getChatlog() const
{
    return new GroupChatLog(talkable_->gcode());
}

void GroupChatDlg::onMemberAdded(Contact *contact)
{
    model_->addContactItem(contact);

    replaceUnconverId(contact);
}

void GroupChatDlg::onMemberRemoved(Contact *contact)
{

}

void GroupChatDlg::replaceUnconverId(Contact *contact)
{
    if ( unconvert_ids_.indexOf(contact->id()) != -1 )
    {
        msgbrowse_.replaceIdToName(contact->id(), contact->markname());
    }
}

Contact *GroupChatDlg::findContactById(QString id) const
{
    return ((Group *)talkable_)->member(id);
}

void GroupChatDlg::onTalkableDataChanged(QVariant data, TalkableDataRole role)
{
    switch ( role )
    {
        case TDR_Avatar:
            ui->lbl_avatar_->setPixmap(data.value<QPixmap>());
            break;
        case TDR_Announcement:
            ui->announcement->setPlainText(data.toString());
            break;
        default:
            break;
    }
}

void GroupChatDlg::onGroupMemberDataChanged(Contact *member, TalkableDataRole role)
{
    model_->talkableDataChanged(member->id(), member->avatar(), role);
}

Contact *GroupChatDlg::getSender(const QString &id) const
{
    return findContactById(id);
}