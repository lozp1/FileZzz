#pragma once
#include <string>
#include <vector>

enum Language {
    LANG_ES = 0,
    LANG_EN,
    LANG_PT,
    LANG_QU,
    LANG_MYN,
    LANG_FR,
    LANG_DE,
    LANG_IT,
    LANG_RU,
    LANG_NL,
    LANG_TR,
    LANG_COUNT
};

struct Translation {
    const char* badge;
    const char* lang_name;
    const char* app_name;
    const char* status_online;
    
    // Dashboard
    const char* dash_title;
    const char* dash_sub;
    const char* menu_explorer;
    const char* menu_explorer_sub;
    const char* menu_mtp;
    const char* menu_mtp_sub;
    const char* menu_ftp;
    const char* menu_ftp_sub;
    const char* menu_lang;
    const char* menu_lang_sub;
    const char* menu_about;
    const char* menu_about_sub;

    // Explorer
    const char* exp_root;
    const char* exp_empty;
    const char* exp_items;
    const char* exp_copied_badge;
    const char* exp_cut_badge;

    // Context Menu
    const char* ctx_title;
    const char* ctx_copy;
    const char* ctx_cut;
    const char* ctx_paste;
    const char* ctx_rename;
    const char* ctx_delete;
    const char* ctx_props;
    const char* ctx_cancel;

    // Confirm Delete Modal
    const char* del_title;
    const char* del_msg;
    const char* del_confirm;
    const char* del_cancel;

    // Properties Modal
    const char* prop_title;
    const char* prop_name;
    const char* prop_path;
    const char* prop_size;
    const char* prop_type;
    const char* prop_date;
    const char* prop_type_folder;
    const char* prop_type_file;

    // MTP & FTP
    const char* mtp_title;
    const char* mtp_active;
    const char* mtp_standby;
    const char* mtp_hint;
    const char* ftp_title;
    const char* ftp_active;
    const char* ftp_standby;

    // About
    const char* about_title;
    const char* about_author_lbl;
    const char* about_author;
    const char* about_email_lbl;
    const char* about_email;
    const char* about_github_lbl;
    const char* about_github;
    const char* about_system_lbl;
    const char* about_system;
    // About extra labels (web, suite, portfolio, engine)
    const char* about_web_lbl;
    const char* about_suite_lbl;
    const char* about_portfolio_lbl;
    const char* about_engine_lbl;

    // Hints
    const char* hint_back;
    const char* hint_select;
    // Theme screen
    const char* menu_theme;
    const char* menu_theme_sub;
    // Exit
    const char* menu_exit;
    const char* menu_exit_sub;
    // Exit confirm modal
    const char* exit_confirm_title;
    const char* exit_confirm_msg;
    const char* exit_confirm_yes;
    const char* exit_confirm_no;
    // Toast messages (fully i18n'd)
    const char* toast_theme;
    const char* toast_lang;
    const char* toast_deleted;
    const char* toast_renamed;
    const char* toast_rename_err;
    const char* toast_copied;
    const char* toast_copy_err;
    const char* toast_moved;
    const char* toast_ftp_on;
    const char* toast_ftp_off;
};

inline const Translation TRANSLATIONS[LANG_COUNT] = {
    // 0: ES - Español
    {
        "[ES]", "Español", "EZ FILES · ANDROMEDA", "SISTEMA ACTIVO",
        "CENTRO DE GESTIÓN Y ALMACENAMIENTO", "Exploración de alta velocidad, transferencia USB MTP y red",
        "Explorador MicroSD", "Navega por tu tarjeta SD, gestiona archivos y carpetas",
        "Conexión USB (MTP)", "Transferencia directa de alta velocidad con PC o Mac",
        "Servidor FTP / Red", "Acceso inalámbrico por red local sin cables",
        "Idioma del Sistema", "Configura el idioma oficial de toda la aplicación",
        "Acerca de", "Información del desarrollador, versión y sistema",
        "Directorio Raíz", "Carpeta vacía", "elementos", "COPIADO:", "CORTADO:",
        "ACCIONES DE ARCHIVO", "Copiar elemento", "Cortar elemento", "Pegar elemento aquí",
        "Renombrar elemento", "Eliminar elemento", "Propiedades del archivo", "Cerrar menú",
        "CONFIRMAR ELIMINACIÓN", "¿Estás seguro de que deseas eliminar permanentemente?",
        "[A] Eliminar Definitivamente", "[B] Cancelar y Conservar",
        "PROPIEDADES", "NOMBRE:", "RUTA:", "TAMAÑO:", "TIPO:", "FECHA MODIF.:",
        "Directorio de Archivos", "Archivo de Datos",
        "RESPONDEDOR USB (MTP)", "ESTADO: ACTIVO (Conectado a PC)", "ESTADO: EN ESPERA (Presiona [A] para Activar)",
        "Conecta el cable USB-C a tu ordenador para gestionar archivos",
        "SERVIDOR FTP INALÁMBRICO", "ESTADO: SERVIDOR ACTIVO", "ESTADO: EN ESPERA (Presiona [A] para Iniciar)",
        "INFORMACIÓN DEL SISTEMA", "DESARROLLADO POR:", "Franco Paolo López Gálvez",
        "CORREO PERSONAL:", "francopaolo_lg@outlook.com", "PÁGINA GITHUB:", "https://github.com/lozp1",
        "SISTEMA OPERATIVO:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "Presiona [B] para Volver", "Presiona [A] para Seleccionar",
        // About extra labels
        "SITIO WEB:", "PROYECTO:", "PORTAFOLIO:", "MOTOR:",
        // Theme + Exit + Toasts
        "Temas del Sistema", "Personaliza la apariencia visual del sistema",
        "Salir al Homebrew Menu", "Regresar al gestor de homebrew",
        "CONFIRMAR SALIDA", "Deseas salir y regresar al Homebrew Menu?",
        "[A] Salir Ahora", "[B] Cancelar",
        "TEMA: ", "IDIOMA: ",
        "ELEMENTO ELIMINADO", "NOMBRE ACTUALIZADO", "ERROR AL RENOMBRAR",
        "ARCHIVO COPIADO", "ERROR EN COPIA", "ARCHIVO MOVIDO",
        "SERVIDOR FTP ACTIVO", "SERVIDOR FTP DETENIDO"
    },
    // 1: EN - English
    {
        "[EN]", "English", "EZ FILES · ANDROMEDA", "SYSTEM ONLINE",
        "STORAGE & FILE MANAGEMENT CENTER", "High-speed browsing, USB MTP transfer and local network",
        "MicroSD Explorer", "Browse your SD card, manage files and folders",
        "USB Connection (MTP)", "High-speed direct file transfer with PC or Mac",
        "FTP Server / Network", "Wireless access over your local WiFi network",
        "System Language", "Select your preferred language across the entire suite",
        "About", "Developer info, system version and build details",
        "Root Directory", "Empty directory", "items", "COPIED:", "CUT:",
        "FILE ACTIONS", "Copy item", "Cut item", "Paste item here",
        "Rename item", "Delete item", "File properties", "Close menu",
        "CONFIRM DELETION", "Are you sure you want to permanently delete this item?",
        "[A] Permanently Delete", "[B] Cancel and Keep",
        "PROPERTIES", "NAME:", "PATH:", "SIZE:", "TYPE:", "MODIFIED:",
        "File Directory", "Data File",
        "USB RESPONDER (MTP)", "STATUS: ACTIVE (Connected to PC)", "STATUS: STANDBY (Press [A] to Enable)",
        "Connect USB-C cable to your PC to manage files",
        "WIRELESS FTP SERVER", "STATUS: SERVER ACTIVE", "STATUS: STANDBY (Press [A] to Start)",
        "SYSTEM INFORMATION", "DEVELOPED BY:", "Franco Paolo López Gálvez",
        "PERSONAL EMAIL:", "francopaolo_lg@outlook.com", "GITHUB PAGE:", "https://github.com/lozp1",
        "OPERATING SYSTEM:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "Press [B] to Return", "Press [A] to Select",
        // About extra labels
        "WEBSITE:", "PROJECT:", "PORTFOLIO:", "ENGINE:",
        // Theme + Exit + Toasts
        "System Themes", "Customize the visual appearance",
        "Exit to Homebrew Menu", "Return to the homebrew launcher",
        "CONFIRM EXIT", "Exit to Homebrew Menu?",
        "[A] Exit Now", "[B] Cancel",
        "THEME: ", "LANGUAGE: ",
        "ITEM DELETED", "NAME UPDATED", "RENAME ERROR",
        "FILE COPIED", "COPY ERROR", "FILE MOVED",
        "FTP SERVER ACTIVE", "FTP SERVER STOPPED"
    },
    // 2: PT - Português
    {
        "[PT]", "Português", "EZ FILES · ANDROMEDA", "SISTEMA ATIVO",
        "CENTRO DE GESTÃO E ARMAZENAMENTO", "Exploração rápida, transferência USB MTP e rede local",
        "Explorador MicroSD", "Navegue no cartão SD, gerencie arquivos e pastas",
        "Conexão USB (MTP)", "Transferência direta em alta velocidade com PC ou Mac",
        "Servidor FTP / Rede", "Acesso sem fio pela rede local sem cabos",
        "Idioma do Sistema", "Selecione o idioma de sua preferência para o sistema",
        "Sobre", "Informações do desenvolvedor, versão e sistema",
        "Diretório Raiz", "Pasta vazia", "itens", "COPIADO:", "RECORTADO:",
        "AÇÕES DO ARQUIVO", "Copiar item", "Recortar item", "Colar item aqui",
        "Renomear item", "Excluir item", "Propriedades do arquivo", "Fechar menu",
        "CONFIRMAR EXCLUSÃO", "Tem certeza de que deseja excluir permanentemente?",
        "[A] Excluir Definitivamente", "[B] Cancelar e Manter",
        "PROPRIEDADES", "NOME:", "CAMINHO:", "TAMANHO:", "TIPO:", "MODIFICADO:",
        "Diretório de Arquivos", "Arquivo de Dados",
        "RESPONDEDOR USB (MTP)", "STATUS: ATIVO (Conectado ao PC)", "STATUS: EM ESPERA (Pressione [A] para Ativar)",
        "Conecte o cabo USB-C ao seu computador para gerenciar arquivos",
        "SERVIDOR FTP SEM FIO", "STATUS: SERVIDOR ATIVO", "STATUS: EM ESPERA (Pressione [A] para Iniciar)",
        "INFORMAÇÕES DO SISTEMA", "DESENVOLVIDO POR:", "Franco Paolo López Gálvez",
        "E-MAIL PESSOAL:", "francopaolo_lg@outlook.com", "PÁGINA GITHUB:", "https://github.com/lozp1",
        "SISTEMA OPERACIONAL:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "Pressione [B] para Voltar", "Pressione [A] para Selecionar",
        "Temas do Sistema", "Personalizar aparencia visual", "Sair para Homebrew Menu", "Retornar ao homebrew", "CONFIRMAR SAIDA", "Sair para o Homebrew Menu?", "[A] Sair Agora", "[B] Cancelar", "TEMA: ", "IDIOMA: ", "ITEM EXCLUIDO", "NOME ATUALIZADO", "ERRO AO RENOMEAR", "ARQUIVO COPIADO", "ERRO NA COPIA", "ARQUIVO MOVIDO", "SERVIDOR FTP ATIVO", "SERVIDOR FTP PARADO"
    },
    // 3: QU - Quechua
    {
        "[QU]", "Runasimi", "EZ FILES · ANDROMEDA", "LLIKA KAWSAQ",
        "KAMACHIY WAN HALLCH'ANA HATUN WASI", "Utqay puriy, USB MTP chaskiy llika ukhupi",
        "MicroSD Qhawaq", "SD k'ipakunata qhaway, allichay willakuykunata",
        "USB T'inkiy (MTP)", "Kikin t'inkiy antanikurwan utqaylla",
        "FTP Llikacha / Wayra", "Mana watayuq t'inkiy llika ukhupi",
        "Llikachap Simin", "Akllay llikachapaq allin simita",
        "Kaymanta", "Ruwaqmanta willakuy, llikachap kawsaynin",
        "Saphi Wayqa", "Ch'usaq wayqa", "k'ipakuna", "ISPAY:", "KUCHUY:",
        "K'IPAP RUWAYNINKUNA", "K'ipata ispay", "K'ipata kuchuy", "Kaypi k'ipata k'askachiy",
        "K'ipap sutin allichay", "K'ipata pichay", "K'ipap kayninkuna", "Wichq'ay",
        "PICHAYTA CHIQAPCHAY", "¿Chiqaptachu kay k'ipata pichayta munanki?",
        "[A] Chiqap Pichay", "[B] Saqiy",
        "KAYNINKUNA", "SUTIN:", "ÑAN:", "HATUNKAY:", "NIRAQ:", "PUNCHAW:",
        "K'ipakunap Wayqan", "Waqaychasqa K'ipa",
        "USB CHASKIQ (MTP)", "STATUS: KAWSAQ (PC-wan T'inkisqa)", "STATUS: SUYACHKAN ([A] Ñit'iy Kawsananpaq)",
        "USB-C ch'ankata antanikuman t'inkiy",
        "FTP WAYRA LLIKACHA", "STATUS: LLIKACHA KAWSAQ", "STATUS: SUYACHKAN ([A] Ñit'iy Qallarinanpaq)",
        "LLIKACHAP WILLAYNIN", "RUWAQ:", "Franco Paolo López Gálvez",
        "KIKIN CHASKI:", "francopaolo_lg@outlook.com", "GITHUB P'ANQA:", "https://github.com/lozp1",
        "LLIKACHA NIRAQ:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Ñit'iy Kutipunapaq", "[A] Ñit'iy Akllanapaq",
        "Rikchariy Ch uya", "Rikchariy allichay", "Llukshiy Homebrew-man", "Kutiy homebrew-man", "LLUQSIYTA", "Homebrew Menu-man lluqsiyta?", "[A] Lluqsiy", "[B] Mana", "RIKCHARIY: ", "SIMI: ", "QICHUSQA", "SUTIN MUSUQYASQA", "PANTASQA", "WASKARISQA", "PANTASQA WASKAY", "APASQA", "FTP KAWSAQ", "FTP TUKUSQA"
    },
    // 4: MYN - Maya K'iche'
    {
        "[MYN]", "K'iche'", "EZ FILES · ANDROMEDA", "KEMB'AL K'ASLIK",
        "UK'UX CHOMANIB'AL YAKB'AL", "Aninb'al rilik wuj, USB MTP k'olb'alil",
        "MicroSD Rilik Wuj", "Chawila' le wuj pa le SD k'olb'al",
        "USB T'ikonik (MTP)", "Aninaq k'olb'al ruk' kematz'ib'",
        "FTP K'amq'aq / Ch'ob'oj", "T'ikonik chi ja'tzil k'amq'aq",
        "Ch'ab'al Kemb'alil", "Chacha' le ch'ab'al chi kemb'alil",
        "Chirij", "Rutzijol le b'anal re, unimal kemb'al",
        "Nab'e K'olb'al", "Ch'usaq yakb'al", "wujilal", "ESAXIK:", "QUPINIK:",
        "CHOMANIB'AL WUJ", "Uqasaxik wuj", "Uqupixik wuj", "Utz'apixik wuj waral",
        "Uk'exik b'i'aj", "Usachik wuj", "Ub'antajik wuj", "Utz'apixik chomanib'al",
        "UCHOLIL SACHIK", "¿La kasik'ij che kasach b'anom?",
        "[A] Qasachik Tz'aqat", "[B] Q'atexik",
        "UB'ANTAJIK", "B'I'AJ:", "B'EY:", "UNIMAL:", "UWIKIK:", "Q'IJ:",
        "Yakb'al Wuj", "Wuj Re Kemb'al",
        "USB CHASKIQ (MTP)", "STATUS: K'ASLIK (Kematz'ib')", "STATUS: EYE'XIK ([A] Pitz'ik)",
        "Chachapa' USB-C kemb'al ruk' kematz'ib'",
        "FTP SILOB'AL", "STATUS: SILOB'AL K'ASLIK", "STATUS: EYE'XIK ([A] Pitz'ik)",
        "RUTZIJOL KEMB'AL", "B'ANAL RE:", "Franco Paolo López Gálvez",
        "TAQIK WUJ:", "francopaolo_lg@outlook.com", "GITHUB:", "https://github.com/lozp1",
        "KEMB'ALIL:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Pitz'ik che Tzalijik", "[A] Pitz'ik che Cha'ik",
        "Temas k iin", "B eyaj k iin", "El Homebrew-el", "Ko ox homebrew-el", "CONFIMAR EL", "Homebrew Menu-el?", "[A] El bey", "[B] Ma", "K IIN: ", "T AAN: ", "TS AABI", "K AAT MEYAJI", "MEYAJ KIME", "XOKBI", "XOKBI KIME", "XOKNABI", "FTP KUXTAL", "FTP SUUBI"
    },
    // 5: FR - Français
    {
        "[FR]", "Français", "EZ FILES · ANDROMEDA", "SYSTÈME ACTIF",
        "CENTRE DE GESTION ET STOCKAGE", "Exploration ultra-rapide, transfert USB MTP et réseau local",
        "Explorateur MicroSD", "Parcourez votre carte SD, gérez fichiers et dossiers",
        "Connexion USB (MTP)", "Transfert direct ultra-rapide avec PC ou Mac",
        "Serveur FTP / Réseau", "Accès sans fil sur réseau local",
        "Langue du Système", "Sélectionnez votre langue pour l'application",
        "À Propos", "Informations du développeur, version et système",
        "Répertoire Racine", "Dossier vide", "éléments", "COPIÉ:", "COUPÉ:",
        "ACTIONS SUR FICHIER", "Copier élément", "Couper élément", "Coller ici",
        "Renommer élément", "Supprimer élément", "Propriétés du fichier", "Fermer menu",
        "CONFIRMER LA SUPPRESSION", "Voulez-vous supprimer cet élément définitivement?",
        "[A] Supprimer Définitivement", "[B] Annuler et Conserver",
        "PROPRIÉTÉS", "NOM:", "CHEMIN:", "TAILLE:", "TYPE:", "MODIFIÉ LE:",
        "Répertoire de Fichiers", "Fichier de Données",
        "RÉPONDEUR USB (MTP)", "STATUT: ACTIF (Connecté au PC)", "STATUT: EN ATTENTE ([A] pour Activer)",
        "Connectez le câble USB-C à votre PC pour transférer",
        "SERVEUR FTP SANS FIL", "STATUT: SERVEUR ACTIF", "STATUT: EN ATTENTE ([A] pour Démarrer)",
        "INFORMATIONS DU SYSTÈME", "DÉVELOPPÉ PAR:", "Franco Paolo López Gálvez",
        "COURRIEL PERSONNEL:", "francopaolo_lg@outlook.com", "PAGE GITHUB:", "https://github.com/lozp1",
        "SYSTÈME D'EXPLOITATION:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Retour", "[A] Sélectionner",
        "Themes du Systeme", "Personnaliser l apparence", "Quitter vers Homebrew Menu", "Retourner au gestionnaire", "CONFIRMER LA SORTIE", "Quitter vers Homebrew Menu?", "[A] Quitter Maintenant", "[B] Annuler", "THEME: ", "LANGUE: ", "ELEMENT SUPPRIME", "NOM MIS A JOUR", "ERREUR RENOMMAGE", "FICHIER COPIE", "ERREUR COPIE", "FICHIER DEPLACE", "SERVEUR FTP ACTIF", "SERVEUR FTP ARRETE"
    },
    // 6: DE - Deutsch
    {
        "[DE]", "Deutsch", "EZ FILES · ANDROMEDA", "SYSTEM ONLINE",
        "DATEIVERWALTUNGSZENTRALE", "High-Speed-Browsing, USB MTP-Übertragung und lokales Netzwerk",
        "MicroSD Explorer", "SD-Karte durchsuchen, Dateien und Ordner verwalten",
        "USB-Verbindung (MTP)", "Direkte High-Speed-Übertragung mit PC oder Mac",
        "FTP-Server / Netzwerk", "Drahtloser Zugriff über lokales WLAN",
        "Systemsprache", "Offizielle Systemsprache konfigurieren",
        "Über", "Entwicklerinfo, Systemversion und Build-Details",
        "Stammverzeichnis", "Leerer Ordner", "Elemente", "KOPIERT:", "AUSGESCHNITTEN:",
        "DATEIAKTIONEN", "Element kopieren", "Element ausschneiden", "Hier einfügen",
        "Element umbenennen", "Element löschen", "Dateieigenschaften", "Menü schließen",
        "LÖSCHEN BESTÄTIGEN", "Möchten Sie dieses Element wirklich dauerhaft löschen?",
        "[A] Dauerhaft löschen", "[B] Abbrechen",
        "EIGENSCHAFTEN", "NAME:", "PFAD:", "GRÖSSE:", "TYP:", "GEÄNDERT:",
        "Ordner", "Datendatei",
        "USB RESPONDER (MTP)", "STATUS: AKTIV (Mit PC verbunden)", "STATUS: BEREIT ([A] zum Aktivieren)",
        "USB-C-Kabel an PC anschließen",
        "DRAHTLOSER FTP-SERVER", "STATUS: SERVER AKTIV", "STATUS: BEREIT ([A] zum Starten)",
        "SYSTEMINFORMATIONEN", "ENTWICKELT VON:", "Franco Paolo López Gálvez",
        "PERSÖNLICHE E-MAIL:", "francopaolo_lg@outlook.com", "GITHUB-SEITE:", "https://github.com/lozp1",
        "BETRIEBSSYSTEM:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Zurück", "[A] Auswählen",
        "Systemdesigns", "Erscheinungsbild anpassen", "Homebrew Menu verlassen", "Zum Homebrew zurueck", "BEENDEN BESTAETIGEN", "Homebrew Menu wechseln?", "[A] Jetzt Beenden", "[B] Abbrechen", "DESIGN: ", "SPRACHE: ", "ELEMENT GELOESCHT", "NAME AKTUALISIERT", "UMBENENNUNGSFEHLER", "DATEI KOPIERT", "KOPIERFEHLER", "DATEI VERSCHOBEN", "FTP-SERVER AKTIV", "FTP-SERVER GESTOPPT"
    },
    // 7: IT - Italiano
    {
        "[IT]", "Italiano", "EZ FILES · ANDROMEDA", "SISTEMA ATTIVO",
        "CENTRO GESTIONE E ARCHIVIAZIONE", "Esplorazione veloce, trasferimento USB MTP e rete",
        "Esplora MicroSD", "Sfoglia la scheda SD, gestisci file e cartelle",
        "Connessione USB (MTP)", "Trasferimento diretto ad alta velocità con PC o Mac",
        "Server FTP / Rete", "Accesso wireless tramite rete locale",
        "Lingua del Sistema", "Imposta la lingua ufficiale dell'applicazione",
        "Info su", "Informazioni sviluppatore, versione e sistema",
        "Directory Principale", "Cartella vuota", "elementi", "COPIATO:", "TAGLIATO:",
        "AZIONI FILE", "Copia elemento", "Taglia elemento", "Incolla qui",
        "Rinomina elemento", "Elimina elemento", "Proprietà file", "Chiudi menu",
        "CONFERMA ELIMINAZIONE", "Sei sicuro di voler eliminare definitivamente questo elemento?",
        "[A] Elimina Definitivamente", "[B] Annulla",
        "PROPRIETÀ", "NOME:", "PERCORSO:", "DIMENSIONE:", "TIPO:", "MODIFICATO:",
        "Cartella", "File di Dati",
        "RESPONDER USB (MTP)", "STATO: ATTIVO (Collegato al PC)", "STATO: IN ATTESA ([A] per Attivare)",
        "Collega il cavo USB-C al computer",
        "SERVER FTP WIRELESS", "STATO: SERVER ATTIVO", "STATO: IN ATTESA ([A] per Avviare)",
        "INFORMAZIONI DI SISTEMA", "SVILUPPATO DA:", "Franco Paolo López Gálvez",
        "EMAIL PERSONALE:", "francopaolo_lg@outlook.com", "PAGINA GITHUB:", "https://github.com/lozp1",
        "SISTEMA OPERATIVO:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Indietro", "[A] Seleziona",
        "Temi di Sistema", "Personalizza l aspetto", "Esci al Homebrew Menu", "Torna al launcher", "CONFERMA USCITA", "Uscire al Homebrew Menu?", "[A] Esci Ora", "[B] Annulla", "TEMA: ", "LINGUA: ", "ELEMENTO ELIMINATO", "NOME AGGIORNATO", "ERRORE DI RINOMINA", "FILE COPIATO", "ERRORE DI COPIA", "FILE SPOSTATO", "SERVER FTP ATTIVO", "SERVER FTP FERMATO"
    },
    // 8: RU - Русский
    {
        "[RU]", "Русский", "EZ FILES · ANDROMEDA", "СИСТЕМА АКТИВНА",
        "ЦЕНТР УПРАВЛЕНИЯ ФАЙЛАМИ", "Скоростной проводник, передача по USB MTP и сети",
        "Проводник MicroSD", "Просмотр SD-карты, управление файлами и папками",
        "Подключение USB (MTP)", "Прямая передача файлов на высокой скорости",
        "FTP Сервер / Сеть", "Беспроводной доступ по локальной сети",
        "Язык системы", "Выберите язык интерфейса приложения",
        "О программе", "Информация о разработчике и системе",
        "Корень диска", "Пустая папка", "элементов", "СКОПИРОВАНО:", "ВЫРЕЗАНО:",
        "ДЕЙСТВИЯ", "Копировать", "Вырезать", "Вставить сюда",
        "Переименовать", "Удалить", "Свойства файла", "Закрыть",
        "ПОДТВЕРЖДЕНИЕ УДАЛЕНИЯ", "Вы уверены, что хотите удалить этот элемент?",
        "[A] Удалить навсегда", "[B] Отмена",
        "СВОЙСТВА", "ИМЯ:", "ПУТЬ:", "РАЗМЕР:", "ТИП:", "ИЗМЕНЕН:",
        "Папка с файлами", "Файл данных",
        "USB MTP РЕСПОНДЕР", "СТАТУС: АКТИВЕН (Подключен к ПК)", "СТАТУС: ОЖИДАНИЕ ([A] для включения)",
        "Подключите кабель USB-C к компьютеру",
        "БЕСПРОВОДНОЙ FTP СЕРВЕР", "СТАТУС: СЕРВЕР АКТИВЕН", "СТАТУС: ОЖИДАНИЕ ([A] для запуска)",
        "О СИСТЕМЕ", "РАЗРАБОТЧИК:", "Franco Paolo López Gálvez",
        "ЛИЧНАЯ ПОЧТА:", "francopaolo_lg@outlook.com", "СТРАНИЦА GITHUB:", "https://github.com/lozp1",
        "ОС И ЯДРО:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Назад", "[A] Выбрать",
        "Temiy sistemy", "Nastroit vneshni vid", "Vykhod v Homebrew", "Vernutsya k homebrew", "PODTVERDIT VYKHOD", "Vykhodit v Homebrew?", "[A] Vyyti seychas", "[B] Otmena", "TEMA: ", "YAZYK: ", "ELEMENT UDALEN", "IMYA OBNOVLENO", "OSHIBKA PEREYM.", "FAYL SKOPIROVAN", "OSHIBKA KOPIR.", "FAYL PEREMESHCHEN", "FTP AKTIVEN", "FTP OSTANOVLEN"
    },
    // 9: NL - Nederlands
    {
        "[NL]", "Nederlands", "EZ FILES · ANDROMEDA", "SYSTEEM ONLINE",
        "BESTANDSBEHEERCENTRUM", "Hogesnelheidsverkenner, USB MTP-overdracht en netwerk",
        "MicroSD Verkenner", "Blader door SD-kaart, beheer bestanden en mappen",
        "USB-verbinding (MTP)", "Directe bestandsoverdracht met pc of Mac",
        "FTP-server / Netwerk", "Draadloze toegang via lokaal netwerk",
        "Systeemtaal", "Selecteer uw voorkeurstaal voor de applicatie",
        "Over", "Ontwikkelaarsinfo, systeemversie en details",
        "Hoofdmap", "Lege map", "items", "GEKOPIEERD:", "GEKNIPT:",
        "BESTANDSACTIES", "Kopiëren", "Knippen", "Hier plakken",
        "Hernoemen", "Verwijderen", "Eigenschappen", "Sluiten",
        "VERWIJDERING BEVESTIGEN", "Weet u zeker dat u dit permanent wilt verwijderen?",
        "[A] Definitief verwijderen", "[B] Annuleren",
        "EIGENSCHAPPEN", "NAAM:", "PAD:", "GROOTTE:", "TYPE:", "GEWIJZIGD:",
        "Map", "Gegevensbestand",
        "USB RESPONDER (MTP)", "STATUS: ACTIEF (Verbonden)", "STATUS: STAND-BY ([A] om in te schakelen)",
        "Sluit USB-C-kabel aan op pc",
        "DRAADLOZE FTP-SERVER", "STATUS: SERVER ACTIEF", "STATUS: STAND-BY ([A] om te starten)",
        "SYSTEEMINFORMATIE", "ONTWIKKELD DOOR:", "Franco Paolo López Gálvez",
        "PERSOONLIJKE E-MAIL:", "francopaolo_lg@outlook.com", "GITHUB-PAGINA:", "https://github.com/lozp1",
        "BESTURINGSSYSTEEM:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Terug", "[A] Selecteren",
        "Systeemthemas", "Pas de weergave aan", "Verlaten naar Homebrew", "Terug naar homebrew", "VERLATEN BEVESTIGEN", "Naar Homebrew Menu?", "[A] Nu Verlaten", "[B] Annuleren", "THEMA: ", "TAAL: ", "ITEM VERWIJDERD", "NAAM BIJGEWERKT", "HERNOEM FOUT", "BESTAND GEKOPIEERD", "KOPIEER FOUT", "BESTAND VERPLAATST", "FTP SERVER ACTIEF", "FTP SERVER GESTOPT"
    },
    // 10: TR - Türkçe
    {
        "[TR]", "Türkçe", "EZ FILES · ANDROMEDA", "SİSTEM ÇEVRİMİÇİ",
        "DEPOLAMA VE DOSYA YÖNETİMİ", "Yüksek hızlı tarama, USB MTP aktarımı ve yerel ağ",
        "MicroSD Gezgini", "SD kartınızı tarayın, dosya ve klasörleri yönetin",
        "USB Bağlantısı (MTP)", "PC veya Mac ile yüksek hızlı doğrudan aktarım",
        "FTP Sunucusu / Ağ", "Yerel WiFi ağı üzerinden kablosuz erişim",
        "Sistem Dili", "Uygulama dilini yapılandırın",
        "Hakkında", "Geliştirici bilgisi, sürüm ve sistem detayları",
        "Kök Dizin", "Boş klasör", "öğe", "KOPYALANDI:", "KESİLDİ:",
        "DOSYA EYLEMLERİ", "Öğeyi kopyala", "Öğeyi kes", "Buraya yapıştır",
        "Yeniden adlandır", "Öğeyi sil", "Dosya özellikleri", "Menüyü kapat",
        "SİLMEYİ ONAYLA", "Bu öğeyi kalıcı olarak silmek istediğinizden emin misiniz?",
        "[A] Kalıcı Olarak Sil", "[B] İptal Et",
        "ÖZELLİKLER", "AD:", "YOL:", "BOYUT:", "TÜR:", "DEĞİŞTİRİLME:",
        "Dizin", "Veri Dosyası",
        "USB RESPONDER (MTP)", "DURUM: AKTİF (PC Bağlı)", "DURUM: BEKLEMEDE ([A] ile Aç)",
        "Dosyaları yönetmek için USB-C kablosunu bağlayın",
        "KABLOSUZ FTP SUNUCUSU", "DURUM: SUNUCU AKTİF", "DURUM: BEKLEMEDE ([A] ile Başlat)",
        "SİSTEM BİLGİSİ", "GELİŞTİRİCİ:", "Franco Paolo López Gálvez",
        "KİŞİSEL E-POSTA:", "francopaolo_lg@outlook.com", "GITHUB SAYFASI:", "https://github.com/lozp1",
        "İŞLETİM SİSTEMİ:", "Horizon OS 22.5.0 | Atmosphere 1.11.2",
        "[B] Geri", "[A] Seç",
        "Sistem Temalar", "Gorsel ozellestir", "Homebrew ye Cik", "Homebrew ya don", "CIKISI ONAYLA", "Homebrew Menuve cikis?", "[A] Simdi Cik", "[B] Iptal", "TEMA: ", "DIL: ", "OGE SILINDI", "AD GUNCELLENDI", "YENIDEN ADL. HATASI", "DOSYA KOPYALANDI", "KOPYALAMA HATASI", "DOSYA TASINIDI", "FTP AKTIF", "FTP DURDURULDU"
    }
};




inline Language currentLanguage = LANG_ES;

inline const Translation& tr() {
    return TRANSLATIONS[currentLanguage];
}

inline void setLanguage(Language lang) {
    if (lang >= 0 && lang < LANG_COUNT) currentLanguage = lang;
}
